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

#ifndef PRIVATE_SET_INTERSECTION_CPP_PROGRESS_H_
#define PRIVATE_SET_INTERSECTION_CPP_PROGRESS_H_

#include <atomic>
#include <cstdint>

namespace private_set_intersection {

// A best-effort per-operation progress counter: the number of elements a PSI
// operation has encrypted / re-encrypted / decrypted so far. It is written into
// a caller-owned 32-bit slot -- a JS ArrayBuffer/SharedArrayBuffer element for
// the native addon, a WASM linear-memory word for the emscripten build -- so a
// UI can poll "n so far" against the known input size N. `slot` is null when the
// caller asked for no progress reporting.
//
// This is a UI hint, NOT a synchronization primitive: relaxed memory order,
// updates batched by the callers. Correctness of the PSI protocol never depends
// on it, and it never touches the ciphertext or wire format.
//
// The slot is caller-owned int32 memory; std::atomic<int32_t> is lock-free and
// layout-compatible with int32_t on every target we build (x86-64, arm64,
// wasm32), so a relaxed atomic view over it is well-defined in practice and
// portable across MSVC / GCC / Clang / emscripten. Under a single-threaded
// emscripten build the atomic ops lower to plain loads/stores. The reinterpret
// of the caller's int32 slot as std::atomic<int32_t> is aliasing UB in the
// abstract machine but relies on this layout compatibility; assert it so a
// target that ever violates it fails to compile rather than misbehaving.
static_assert(sizeof(std::atomic<int32_t>) == sizeof(int32_t) &&
                  alignof(std::atomic<int32_t>) == alignof(int32_t),
              "progress slot requires std::atomic<int32_t> layout-compatible "
              "with int32_t");
static_assert(std::atomic<int32_t>::is_always_lock_free,
              "progress slot requires a lock-free std::atomic<int32_t>");

// A monotonically increasing view onto one progress slot, batching updates from
// a single thread (each shard of a parallel loop owns its own ProgressCounter
// over the shared slot). Contention is negligible: one relaxed fetch_add per
// kBatch elements.
class ProgressCounter {
 public:
  explicit ProgressCounter(int32_t* slot) : slot_(slot) {}

  // Non-copyable: a copy would share the slot and both destructors would Flush
  // the buffered count, double-publishing it. (The user-declared destructor
  // already suppresses the implicit move.)
  ProgressCounter(const ProgressCounter&) = delete;
  ProgressCounter& operator=(const ProgressCounter&) = delete;

  // Publishes any buffered count. Call once when the loop finishes.
  ~ProgressCounter() { Flush(); }

  // Records that one more element was processed.
  void Increment() {
    if (slot_ == nullptr) return;
    if (++pending_ >= kBatch) Flush();
  }

  // Publishes the buffered count to the shared slot.
  void Flush() {
    if (slot_ == nullptr || pending_ == 0) return;
    reinterpret_cast<std::atomic<int32_t>*>(slot_)->fetch_add(
        pending_, std::memory_order_relaxed);
    pending_ = 0;
  }

 private:
  static constexpr int32_t kBatch = 256;
  int32_t* slot_;
  int32_t pending_ = 0;
};

// Resets a progress slot to zero at the start of an operation, so a poller never
// observes a previous operation's final count. No-op when `slot` is null.
inline void ResetProgress(int32_t* slot) {
  if (slot != nullptr) {
    reinterpret_cast<std::atomic<int32_t>*>(slot)->store(
        0, std::memory_order_relaxed);
  }
}

}  // namespace private_set_intersection

#endif  // PRIVATE_SET_INTERSECTION_CPP_PROGRESS_H_
