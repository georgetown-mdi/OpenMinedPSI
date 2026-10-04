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

#ifndef PRIVATE_SET_INTERSECTION_CPP_PSI_MATCH_H_
#define PRIVATE_SET_INTERSECTION_CPP_PSI_MATCH_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "private_join_and_compute/crypto/ec_commutative_cipher.h"

namespace private_set_intersection {

// What a finished PsiMatch learned.
struct PsiMatchResult {
  // Response elements decrypted. Each is decrypted exactly once, so this
  // equals the number of entries in the response.
  int64_t decrypted_count = 0;

  // Setup elements equal to at least one decrypted response element. Over a
  // strictly ascending setup this is what PsiClient::GetIntersectionSize
  // returns for the same setup and response, however the response was cut.
  int64_t intersection_size = 0;

  // Identifier-revealing matches only (empty otherwise): one entry per
  // (response index, setup index) pair whose elements are equal, in response
  // order. The pairs are those PsiClient::GetAssociationTable returns.
  std::vector<uint32_t> response_indices;
  std::vector<uint32_t> setup_indices;
};

// The client's final PSI step over a Raw server setup, fed in byte windows so
// neither the setup nor the response is ever held as a parsed protobuf.
//
// The setup is the serialized psi_proto::ServerSetup and the response the
// serialized psi_proto::Response; each may be cut into windows at any byte.
// Use, in order:
//
//   AddSetupBytes   any number of times, the setup's bytes in order;
//   SealSetup       once, after the last setup byte;
//   MatchResponsePiece  any number of times, the response's bytes in order;
//   Finish          once.
//
// The setup is held as its elements only. The response is decrypted as it
// arrives, each element once, and matched by binary search against the
// setup; only the match results are kept.
//
// Only the shape the library itself emits is accepted. The setup is exactly
// one `raw` field holding only `encrypted_elements` entries; the response
// holds only `encrypted_elements` entries. Any other field, an entry longer
// than kMaxElementBytes, an entry that runs past the end of its message, a
// byte after the setup's `raw` field, or a setup whose elements are not
// strictly ascending by unsigned bytes is refused with INVALID_ARGUMENT.
// Calls out of order are refused with FAILED_PRECONDITION. After any refusal
// every later call returns the same status.
//
// Not thread-safe; one caller drives an instance.
class PsiMatch {
 public:
  // The longest element accepted, setup or response: a P-256 point in
  // compressed form, the only encoding the cipher emits.
  static constexpr std::size_t kMaxElementBytes = 33;

  PsiMatch() = delete;
  PsiMatch(const PsiMatch&) = delete;
  PsiMatch& operator=(const PsiMatch&) = delete;
  ~PsiMatch();

  // A match decrypting with the client private key `key_bytes`. Only an
  // identifier-revealing match (`reveal_intersection`) records the pairs.
  static absl::StatusOr<std::unique_ptr<PsiMatch>> Create(
      const std::string& key_bytes, bool reveal_intersection);

  // Appends the next window of the serialized setup.
  absl::Status AddSetupBytes(absl::string_view bytes);

  // Ends the setup. Refused if the bytes so far are not a whole setup.
  absl::Status SealSetup();

  // Decrypts and matches the next window of the serialized response. An
  // element cut by the window's end is completed by the next window.
  // `progress`, when non-null, has the count of elements this call decrypts
  // added to it (a UI hint; see progress.h).
  absl::Status MatchResponsePiece(absl::string_view bytes,
                                  int32_t* progress = nullptr);

  // Ends the response and returns the result. Refused if the response bytes
  // end inside an element. Frees the setup.
  absl::StatusOr<PsiMatchResult> Finish();

  // Whether this match records the (response, setup) index pairs.
  bool reveal_intersection() const { return reveal_intersection_; }

  // Setup elements added so far.
  int64_t setup_size() const { return static_cast<int64_t>(setup_count_); }

 private:
  // Reads a run of `encrypted_elements` entries (field 1, wire type 2) cut
  // into windows at any byte, handing each whole entry to a callback.
  class EntryReader {
   public:
    // Consumes all of `bytes`, calling `on_entry(absl::string_view)` for each
    // entry it completes. Stops at the first refusal.
    template <typename OnEntry>
    absl::Status Feed(absl::string_view bytes, OnEntry&& on_entry);
    // True when no entry is partly read.
    bool AtEntryBoundary() const { return state_ == State::kTag; }

   private:
    enum class State { kTag, kLength, kBody };
    State state_ = State::kTag;
    uint32_t length_ = 0;
    int length_bytes_ = 0;
    std::string body_;
  };

  enum class Phase { kSetup, kMatching, kFinished };
  enum class SetupFrame { kTag, kLength, kBody, kDone };

  PsiMatch(
      std::unique_ptr<::private_join_and_compute::ECCommutativeCipher> cipher,
      bool reveal_intersection);

  absl::Status Fail(absl::Status status);
  absl::Status AddSetupElement(absl::string_view element);
  absl::Status MatchBatch(int32_t* progress);
  // The setup index holding `element`, or -1.
  int64_t FindInSetup(absl::string_view element) const;
  absl::string_view SetupElement(std::size_t index) const;

  std::unique_ptr<::private_join_and_compute::ECCommutativeCipher> cipher_;
  const bool reveal_intersection_;
  Phase phase_ = Phase::kSetup;
  absl::Status failure_;

  // The setup's framing: the `raw` field's tag and length, then its body.
  SetupFrame setup_frame_ = SetupFrame::kTag;
  uint32_t raw_length_ = 0;
  int raw_length_bytes_ = 0;
  uint64_t raw_remaining_ = 0;
  EntryReader setup_reader_;

  // Setup elements in fixed slots (a length byte, then the element), in
  // blocks so a large setup is never copied to grow.
  std::vector<std::unique_ptr<uint8_t[]>> setup_blocks_;
  std::size_t setup_count_ = 0;

  EntryReader response_reader_;
  std::vector<std::string> batch_;
  int64_t decrypted_count_ = 0;
  std::vector<uint64_t> matched_setup_;
  std::vector<uint32_t> response_indices_;
  std::vector<uint32_t> setup_indices_;
};

}  // namespace private_set_intersection

#endif  // PRIVATE_SET_INTERSECTION_CPP_PSI_MATCH_H_
