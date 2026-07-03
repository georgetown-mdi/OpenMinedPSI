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

#include "private_set_intersection/cpp/parallel_ec.h"

// The threaded implementation exists only for native builds that opt in via
// PSI_ENABLE_THREADS. Under emscripten (WASM, built with USE_PTHREADS=0) this
// translation unit is intentionally empty and the single-threaded loops in
// psi_server.cpp / psi_client.cpp are used instead.
#if defined(PSI_ENABLE_THREADS) && !defined(__EMSCRIPTEN__)

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <thread>
#include <utility>

#include "absl/status/statusor.h"
#include "openssl/mem.h"
#include "openssl/obj_mac.h"
#include "private_set_intersection/cpp/progress.h"

namespace private_set_intersection {
namespace {

using ::private_join_and_compute::ECCommutativeCipher;

// PSI always uses NIST P-256 with SHA-256 (see psi_client.cpp / psi_server.cpp,
// which construct their ciphers with exactly these parameters). A shard cipher
// must be built the same way to reproduce the same ciphertext bytes.
absl::StatusOr<std::unique_ptr<ECCommutativeCipher>> CloneCipher(
    const std::string& key_bytes) {
  return ECCommutativeCipher::CreateFromKey(
      NID_X9_62_prime256v1, key_bytes, ECCommutativeCipher::HashType::SHA256);
}

// Below kMinInputsForThreads inputs the thread spawn + per-shard key setup
// costs outweigh the parallel speedup, so run single-threaded. Otherwise cap
// the thread count so each shard still processes a worthwhile chunk. These are
// throughput tunables, not correctness parameters -- the output is identical
// for any thread count.
constexpr std::size_t kMinInputsForThreads = 1024;
constexpr std::size_t kMinInputsPerThread = 512;

std::size_t ChooseThreadCount(std::size_t n) {
  if (n < kMinInputsForThreads) return 1;
  const unsigned hardware = std::thread::hardware_concurrency();
  const std::size_t hw = hardware == 0 ? 1 : static_cast<std::size_t>(hardware);
  const std::size_t by_work = n / kMinInputsPerThread;
  return std::max<std::size_t>(1, std::min<std::size_t>(hw, by_work));
}

using ElementOp = std::function<absl::StatusOr<std::string>(
    ECCommutativeCipher*, const std::string&)>;

// Applies `op` to every input using `primary` for the first shard and a
// freshly-built cipher for each additional shard, writing results by absolute
// index. Publishes the running processed count to `progress` (the caller has
// already reset it to 0). Returns the first shard error, if any.
absl::Status TransformElements(ECCommutativeCipher* primary,
                               absl::Span<const std::string> inputs,
                               const ElementOp& op, int32_t* progress,
                               std::vector<std::string>* outputs) {
  const std::size_t n = inputs.size();
  outputs->assign(n, std::string());
  if (n == 0) return absl::OkStatus();

  const std::size_t num_threads = ChooseThreadCount(n);
  if (num_threads <= 1) {
    ProgressCounter counter(progress);
    for (std::size_t i = 0; i < n; ++i) {
      absl::StatusOr<std::string> encrypted = op(primary, inputs[i]);
      if (!encrypted.ok()) return encrypted.status();
      (*outputs)[i] = *std::move(encrypted);
      counter.Increment();
    }
    return absl::OkStatus();
  }

  // Contiguous shards. Distinct index ranges write to distinct vector elements,
  // so there is no data race on `outputs` (it is pre-sized, never reallocated).
  std::string key = primary->GetPrivateKeyBytes();
  // Zero this transport copy of the private key when the parallel section exits
  // (every return path below is covered, after the threads are joined). This is
  // incremental, not complete: each per-shard clone cipher holds its own key
  // copy, so threading adds num_threads-1 copies over the single-threaded
  // baseline, and neither those nor the primary cipher's copy is cleansed here
  // because the upstream ECCommutativeCipher does not cleanse its own key on
  // destruction. Eliminating the residual plaintext-key copies is an upstream
  // change that would also cover the single-threaded and WASM builds.
  struct KeyWiper {
    std::string& key;
    ~KeyWiper() { OPENSSL_cleanse(key.data(), key.size()); }
  } key_wiper{key};
  std::vector<std::pair<std::size_t, std::size_t>> ranges(num_threads);
  const std::size_t base = n / num_threads;
  const std::size_t remainder = n % num_threads;
  std::size_t start = 0;
  for (std::size_t t = 0; t < num_threads; ++t) {
    const std::size_t len = base + (t < remainder ? 1 : 0);
    ranges[t] = {start, start + len};
    start += len;
  }

  std::vector<absl::Status> shard_status(num_threads, absl::OkStatus());

  const auto run_shard = [&](std::size_t t) {
    // Convert any exception (e.g. std::bad_alloc constructing a result string)
    // into a shard error: escaping a worker thread it would std::terminate, and
    // escaping the calling thread it would unwind past the join below.
    try {
      ECCommutativeCipher* cipher = primary;
      std::unique_ptr<ECCommutativeCipher> owned;
      if (t != 0) {
        // Shards other than 0 must not touch `primary`'s scratch context.
        absl::StatusOr<std::unique_ptr<ECCommutativeCipher>> clone =
            CloneCipher(key);
        if (!clone.ok()) {
          shard_status[t] = clone.status();
          return;
        }
        owned = *std::move(clone);
        cipher = owned.get();
      }
      // Each shard batches its own contribution into the shared progress slot.
      ProgressCounter counter(progress);
      for (std::size_t i = ranges[t].first; i < ranges[t].second; ++i) {
        absl::StatusOr<std::string> encrypted = op(cipher, inputs[i]);
        if (!encrypted.ok()) {
          shard_status[t] = encrypted.status();
          return;
        }
        (*outputs)[i] = *std::move(encrypted);
        counter.Increment();
      }
    } catch (...) {
      shard_status[t] =
          absl::ResourceExhaustedError("elliptic-curve shard failed");
    }
  };

  std::vector<std::thread> threads;
  threads.reserve(num_threads - 1);
  std::size_t next_shard = 1;
  try {
    for (; next_shard < num_threads; ++next_shard) {
      threads.emplace_back(run_shard, next_shard);
    }
  } catch (...) {
    // std::thread construction can fail (e.g. EAGAIN under thread pressure).
    // Run the shards that could not be spawned on the calling thread below,
    // rather than letting the already-spawned joinable threads reach
    // std::terminate as the vector unwinds. The result is identical, just less
    // parallel.
  }
  run_shard(0);  // the calling thread owns shard 0
  for (std::size_t t = next_shard; t < num_threads; ++t) {
    run_shard(t);  // any shards that were not spawned above
  }
  for (std::thread& thread : threads) thread.join();

  for (const absl::Status& status : shard_status) {
    if (!status.ok()) return status;
  }
  return absl::OkStatus();
}

}  // namespace

absl::Status EncryptElements(ECCommutativeCipher* primary,
                             absl::Span<const std::string> inputs,
                             std::vector<std::string>* outputs,
                             int32_t* progress) {
  return TransformElements(
      primary, inputs,
      [](ECCommutativeCipher* cipher, const std::string& input) {
        return cipher->Encrypt(input);
      },
      progress, outputs);
}

absl::Status ReEncryptElements(ECCommutativeCipher* primary,
                               absl::Span<const std::string> inputs,
                               std::vector<std::string>* outputs,
                               int32_t* progress) {
  return TransformElements(
      primary, inputs,
      [](ECCommutativeCipher* cipher, const std::string& input) {
        return cipher->ReEncrypt(input);
      },
      progress, outputs);
}

absl::Status DecryptElements(ECCommutativeCipher* primary,
                             absl::Span<const std::string> inputs,
                             std::vector<std::string>* outputs,
                             int32_t* progress) {
  return TransformElements(
      primary, inputs,
      [](ECCommutativeCipher* cipher, const std::string& input) {
        return cipher->Decrypt(input);
      },
      progress, outputs);
}

}  // namespace private_set_intersection

#endif  // PSI_ENABLE_THREADS && !__EMSCRIPTEN__
