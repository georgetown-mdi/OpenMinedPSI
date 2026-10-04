//
// Copyright 2020 the authors listed in CONTRIBUTORS.md
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include "private_set_intersection/cpp/psi_match.h"

#include <algorithm>
#include <bitset>
#include <cstring>
#include <limits>
#include <utility>

#include "absl/memory/memory.h"
#include "absl/strings/str_cat.h"
#include "openssl/obj_mac.h"
#include "private_set_intersection/cpp/parallel_ec.h"
#include "private_set_intersection/cpp/progress.h"

namespace private_set_intersection {

namespace {

using ::private_join_and_compute::ECCommutativeCipher;

// The tag byte of field 1 with wire type 2 (length-delimited): `raw` in
// ServerSetup, `encrypted_elements` in RawInfo and in Response.
constexpr uint8_t kField1LengthDelimited = 0x0A;

// A varint holding a uint32 takes at most five bytes.
constexpr int kMaxVarint32Bytes = 5;

// A setup slot: the element's length, then up to kMaxElementBytes of it.
constexpr std::size_t kSlotBytes = 1 + PsiMatch::kMaxElementBytes;
constexpr std::size_t kBlockSlotsLog2 = 16;
constexpr std::size_t kBlockSlots = std::size_t{1} << kBlockSlotsLog2;

// Response elements decrypted together: enough to keep a threaded native
// build's shards busy, few enough that the batch stays a few MB.
constexpr std::size_t kBatchElements = std::size_t{1} << 16;

// The largest count an index in the result can hold.
constexpr uint64_t kMaxElements = std::numeric_limits<uint32_t>::max();

// Unsigned byte order, shorter first on a common prefix: the order
// std::string compares in, which the library sorts a Raw setup by.
int CompareElements(absl::string_view a, absl::string_view b) {
  const std::size_t common = std::min(a.size(), b.size());
  if (common > 0) {
    const int prefix = std::memcmp(a.data(), b.data(), common);
    if (prefix != 0) return prefix;
  }
  if (a.size() == b.size()) return 0;
  return a.size() < b.size() ? -1 : 1;
}

// Accumulates one byte of a varint into `*value`. Returns true once the
// varint is complete; refuses one longer than five bytes or above 32 bits.
absl::StatusOr<bool> AddVarint32Byte(uint8_t byte, uint32_t* value,
                                     int* bytes_read, const char* what) {
  if (*bytes_read == kMaxVarint32Bytes - 1 && (byte & 0xF0) != 0) {
    return absl::InvalidArgumentError(
        absl::StrCat(what, " length does not fit in 32 bits"));
  }
  *value |= static_cast<uint32_t>(byte & 0x7F) << (7 * *bytes_read);
  ++*bytes_read;
  return (byte & 0x80) == 0;
}

}  // namespace

template <typename OnEntry>
absl::Status PsiMatch::EntryReader::Feed(absl::string_view bytes,
                                         OnEntry&& on_entry) {
  std::size_t at = 0;
  while (at < bytes.size()) {
    switch (state_) {
      case State::kTag: {
        if (static_cast<uint8_t>(bytes[at]) != kField1LengthDelimited) {
          return absl::InvalidArgumentError(
              "unexpected field: only encrypted_elements entries are "
              "accepted");
        }
        ++at;
        state_ = State::kLength;
        length_ = 0;
        length_bytes_ = 0;
        break;
      }
      case State::kLength: {
        absl::StatusOr<bool> done =
            AddVarint32Byte(static_cast<uint8_t>(bytes[at]), &length_,
                            &length_bytes_, "element");
        if (!done.ok()) return done.status();
        ++at;
        if (!*done) break;
        if (length_ > kMaxElementBytes) {
          return absl::InvalidArgumentError(
              absl::StrCat("element of ", length_, " bytes is longer than the ",
                           kMaxElementBytes, "-byte maximum"));
        }
        body_.clear();
        state_ = State::kBody;
        if (length_ == 0) {
          state_ = State::kTag;
          absl::Status status = on_entry(absl::string_view());
          if (!status.ok()) return status;
        }
        break;
      }
      case State::kBody: {
        const std::size_t wanted = length_ - body_.size();
        const std::size_t available = bytes.size() - at;
        if (body_.empty() && available >= wanted) {
          // The whole element is in this window: hand it over in place.
          state_ = State::kTag;
          absl::Status status = on_entry(bytes.substr(at, wanted));
          at += wanted;
          if (!status.ok()) return status;
          break;
        }
        const std::size_t take = std::min(wanted, available);
        body_.append(bytes.data() + at, take);
        at += take;
        if (body_.size() == length_) {
          state_ = State::kTag;
          absl::Status status = on_entry(absl::string_view(body_));
          if (!status.ok()) return status;
        }
        break;
      }
    }
  }
  return absl::OkStatus();
}

PsiMatch::PsiMatch(std::unique_ptr<ECCommutativeCipher> cipher,
                   bool reveal_intersection)
    : cipher_(std::move(cipher)), reveal_intersection_(reveal_intersection) {}

PsiMatch::~PsiMatch() = default;

absl::StatusOr<std::unique_ptr<PsiMatch>> PsiMatch::Create(
    const std::string& key_bytes, bool reveal_intersection) {
  // The curve and hash PsiClient uses.
  ASSIGN_OR_RETURN(auto cipher, ECCommutativeCipher::CreateFromKey(
                                    NID_X9_62_prime256v1, key_bytes,
                                    ECCommutativeCipher::HashType::SHA256));
  return absl::WrapUnique(new PsiMatch(std::move(cipher), reveal_intersection));
}

absl::Status PsiMatch::Fail(absl::Status status) {
  failure_ = status;
  return status;
}

absl::Status PsiMatch::AddSetupBytes(absl::string_view bytes) {
  if (!failure_.ok()) return failure_;
  if (phase_ != Phase::kSetup) {
    return Fail(
        absl::FailedPreconditionError("AddSetupBytes called after SealSetup"));
  }
  std::size_t at = 0;
  while (at < bytes.size()) {
    switch (setup_frame_) {
      case SetupFrame::kTag: {
        if (static_cast<uint8_t>(bytes[at]) != kField1LengthDelimited) {
          return Fail(absl::InvalidArgumentError(
              "server setup is not a single Raw data structure"));
        }
        ++at;
        setup_frame_ = SetupFrame::kLength;
        break;
      }
      case SetupFrame::kLength: {
        absl::StatusOr<bool> done =
            AddVarint32Byte(static_cast<uint8_t>(bytes[at]), &raw_length_,
                            &raw_length_bytes_, "server setup");
        if (!done.ok()) return Fail(done.status());
        ++at;
        if (!*done) break;
        raw_remaining_ = raw_length_;
        setup_frame_ =
            raw_remaining_ == 0 ? SetupFrame::kDone : SetupFrame::kBody;
        break;
      }
      case SetupFrame::kBody: {
        const std::size_t take = static_cast<std::size_t>(
            std::min<uint64_t>(raw_remaining_, bytes.size() - at));
        absl::Status status = setup_reader_.Feed(
            bytes.substr(at, take), [this](absl::string_view element) {
              return AddSetupElement(element);
            });
        if (!status.ok()) return Fail(status);
        at += take;
        raw_remaining_ -= take;
        if (raw_remaining_ == 0) {
          if (!setup_reader_.AtEntryBoundary()) {
            return Fail(absl::InvalidArgumentError(
                "a server setup element runs past the end of the Raw data "
                "structure"));
          }
          setup_frame_ = SetupFrame::kDone;
        }
        break;
      }
      case SetupFrame::kDone:
        return Fail(absl::InvalidArgumentError(
            "server setup has bytes after the Raw data structure"));
    }
  }
  return absl::OkStatus();
}

absl::Status PsiMatch::AddSetupElement(absl::string_view element) {
  if (setup_count_ >= kMaxElements) {
    return absl::InvalidArgumentError("server setup has too many elements");
  }
  if (setup_count_ > 0 &&
      CompareElements(SetupElement(setup_count_ - 1), element) >= 0) {
    return absl::InvalidArgumentError(
        "server setup is not in strictly ascending element order");
  }
  const std::size_t block = setup_count_ >> kBlockSlotsLog2;
  if (block == setup_blocks_.size()) {
    setup_blocks_.push_back(
        std::make_unique<uint8_t[]>(kBlockSlots * kSlotBytes));
  }
  uint8_t* slot = setup_blocks_[block].get() +
                  (setup_count_ & (kBlockSlots - 1)) * kSlotBytes;
  slot[0] = static_cast<uint8_t>(element.size());
  if (!element.empty()) std::memcpy(slot + 1, element.data(), element.size());
  ++setup_count_;
  return absl::OkStatus();
}

absl::string_view PsiMatch::SetupElement(std::size_t index) const {
  const uint8_t* slot = setup_blocks_[index >> kBlockSlotsLog2].get() +
                        (index & (kBlockSlots - 1)) * kSlotBytes;
  return absl::string_view(reinterpret_cast<const char*>(slot + 1), slot[0]);
}

absl::Status PsiMatch::SealSetup() {
  if (!failure_.ok()) return failure_;
  if (phase_ != Phase::kSetup) {
    return Fail(
        absl::FailedPreconditionError("SealSetup called more than once"));
  }
  if (setup_frame_ != SetupFrame::kDone) {
    return Fail(absl::InvalidArgumentError("server setup is incomplete"));
  }
  matched_setup_.assign((setup_count_ + 63) / 64, 0);
  phase_ = Phase::kMatching;
  return absl::OkStatus();
}

int64_t PsiMatch::FindInSetup(absl::string_view element) const {
  std::size_t low = 0;
  std::size_t high = setup_count_;
  while (low < high) {
    const std::size_t mid = low + (high - low) / 2;
    const int order = CompareElements(SetupElement(mid), element);
    if (order == 0) return static_cast<int64_t>(mid);
    if (order < 0) {
      low = mid + 1;
    } else {
      high = mid;
    }
  }
  return -1;
}

absl::Status PsiMatch::MatchResponsePiece(absl::string_view bytes,
                                          int32_t* progress) {
  if (!failure_.ok()) return failure_;
  if (phase_ == Phase::kSetup) {
    return Fail(absl::FailedPreconditionError(
        "MatchResponsePiece called before SealSetup"));
  }
  if (phase_ == Phase::kFinished) {
    return Fail(absl::FailedPreconditionError(
        "MatchResponsePiece called after Finish"));
  }
  absl::Status status =
      response_reader_.Feed(bytes, [this, progress](absl::string_view element) {
        if (static_cast<uint64_t>(decrypted_count_) + batch_.size() >=
            kMaxElements) {
          return absl::InvalidArgumentError(
              "server response has too many elements");
        }
        batch_.emplace_back(element);
        if (batch_.size() >= kBatchElements) return MatchBatch(progress);
        return absl::OkStatus();
      });
  if (status.ok()) status = MatchBatch(progress);
  if (!status.ok()) return Fail(status);
  return absl::OkStatus();
}

absl::Status PsiMatch::MatchBatch(int32_t* progress) {
  if (batch_.empty()) return absl::OkStatus();
  std::vector<std::string> decrypted;
#if defined(PSI_ENABLE_THREADS) && !defined(__EMSCRIPTEN__)
  absl::Status status =
      DecryptElements(cipher_.get(), batch_, &decrypted, progress);
  if (!status.ok()) return status;
#else
  decrypted.reserve(batch_.size());
  ProgressCounter counter(progress);
  for (const std::string& element : batch_) {
    ASSIGN_OR_RETURN(std::string plain, cipher_->Decrypt(element));
    decrypted.push_back(std::move(plain));
    counter.Increment();
  }
#endif
  for (std::size_t k = 0; k < decrypted.size(); ++k) {
    const int64_t index = FindInSetup(decrypted[k]);
    if (index < 0) continue;
    matched_setup_[index >> 6] |= uint64_t{1} << (index & 63);
    if (reveal_intersection_) {
      response_indices_.push_back(static_cast<uint32_t>(decrypted_count_ + k));
      setup_indices_.push_back(static_cast<uint32_t>(index));
    }
  }
  decrypted_count_ += static_cast<int64_t>(decrypted.size());
  batch_.clear();
  return absl::OkStatus();
}

absl::StatusOr<PsiMatchResult> PsiMatch::Finish() {
  if (!failure_.ok()) return failure_;
  if (phase_ == Phase::kSetup) {
    return Fail(
        absl::FailedPreconditionError("Finish called before SealSetup"));
  }
  if (phase_ == Phase::kFinished) {
    return Fail(absl::FailedPreconditionError("Finish called more than once"));
  }
  if (!response_reader_.AtEntryBoundary()) {
    return Fail(
        absl::InvalidArgumentError("server response ends inside an element"));
  }
  PsiMatchResult result;
  result.decrypted_count = decrypted_count_;
  for (uint64_t word : matched_setup_) {
    result.intersection_size +=
        static_cast<int64_t>(std::bitset<64>(word).count());
  }
  result.response_indices = std::move(response_indices_);
  result.setup_indices = std::move(setup_indices_);
  std::vector<std::unique_ptr<uint8_t[]>>().swap(setup_blocks_);
  std::vector<uint64_t>().swap(matched_setup_);
  std::vector<std::string>().swap(batch_);
  phase_ = Phase::kFinished;
  return result;
}

}  // namespace private_set_intersection
