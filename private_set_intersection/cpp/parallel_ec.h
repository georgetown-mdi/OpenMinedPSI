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

#ifndef PRIVATE_SET_INTERSECTION_CPP_PARALLEL_EC_H_
#define PRIVATE_SET_INTERSECTION_CPP_PARALLEL_EC_H_

#include <cstdint>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/types/span.h"
#include "private_join_and_compute/crypto/ec_commutative_cipher.h"

namespace private_set_intersection {

// Parallel per-element elliptic-curve transforms for the native (non-WASM)
// builds -- the throughput bottleneck of the PSI protocol is the per-element
// P-256 scalar multiplication in these loops.
//
// Each function applies the corresponding ECCommutativeCipher operation to
// every input and writes the results in INPUT ORDER, so the serialized wire
// bytes are byte-for-byte identical to the single-threaded path (the transform
// is deterministic: H(x)^k with no per-message nonce). The work is sharded
// across up to hardware_concurrency() threads.
//
// ECCommutativeCipher holds a single BN_CTX scratch context that is NOT
// thread-safe, so a shared cipher cannot be used from multiple threads. Each
// shard beyond the first therefore builds its OWN cipher from `primary`'s key:
// the secret key is shared across shards (it is the same process, same key),
// but the mutable scratch context is per-thread. The first shard reuses
// `primary` on the calling thread.
//
// These are compiled and called only under PSI_ENABLE_THREADS (set for the
// native prebuild build); the WASM build keeps the original single-threaded
// loops and never references these symbols.
//
// `progress`, when non-null, is a caller-owned int32 slot into which the
// running count of processed elements is published (see progress.h). Each shard
// batches its own updates, so the slot advances monotonically toward
// inputs.size().

absl::Status EncryptElements(
    ::private_join_and_compute::ECCommutativeCipher* primary,
    absl::Span<const std::string> inputs, std::vector<std::string>* outputs,
    int32_t* progress);

absl::Status ReEncryptElements(
    ::private_join_and_compute::ECCommutativeCipher* primary,
    absl::Span<const std::string> inputs, std::vector<std::string>* outputs,
    int32_t* progress);

absl::Status DecryptElements(
    ::private_join_and_compute::ECCommutativeCipher* primary,
    absl::Span<const std::string> inputs, std::vector<std::string>* outputs,
    int32_t* progress);

}  // namespace private_set_intersection

#endif  // PRIVATE_SET_INTERSECTION_CPP_PARALLEL_EC_H_
