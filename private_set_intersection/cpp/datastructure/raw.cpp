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

#include "private_set_intersection/cpp/datastructure/raw.h"

#include <iostream>

#include <algorithm> // std::sort, maybe std::swap
#include <numeric> // std::iota
#include <cmath>
#include <utility> // maybe std::swap

#include "absl/memory/memory.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_cat.h"
#include "private_set_intersection/proto/psi.pb.h"

namespace private_set_intersection {

// Computes the intersection of two collections. The first collection must be a
// `pair<T, int64_t>`. The `T` must be the same in the second collection.
//
// Requires both collections to be sorted.
//
// Complexity:
// - O(max(n, m))
template <class InputIt1, class InputIt2, class OutputIt>
void custom_set_intersection(InputIt1 first1, InputIt1 last1, InputIt2 first2,
                             InputIt2 last2, OutputIt d_first) {
  while (first1 != last1 && first2 != last2) {
    if ((*first1).first < *first2)
      ++first1;
    else {
      // *first1 and *first2 are equivalent.
      if (!(*first2 < (*first1).first)) {
        *d_first++ = (*first1++).second;
      }
      ++first2;
    }
  }
}

Raw::Raw(std::vector<std::string> elements) : encrypted_(std::move(elements)) {}

// Create is called by the server when constructing its startup message.
// That is, the encrypted version of the server's own data.
StatusOr<std::unique_ptr<Raw>> Raw::Create(
  std::vector<std::string> elements,
  std::vector<std::size_t>* sorting_permutation_ptr
)
{
  std::unique_ptr<std::vector<std::size_t>> local_sorting_permutation(nullptr);
  if (sorting_permutation_ptr == nullptr) {
    local_sorting_permutation = std::unique_ptr<std::vector<std::size_t>>(new std::vector<std::size_t>(elements.size()));
    sorting_permutation_ptr = local_sorting_permutation.get();
  } 
  std::vector<std::size_t>& sorting_permutation(*sorting_permutation_ptr);
  
  std::iota(sorting_permutation.begin(), sorting_permutation.end(), 0);

  std::sort(
    sorting_permutation.begin(), sorting_permutation.end(),
    [&elements](size_t i1, size_t i2) {return elements[i1] < elements[i2];}
  );

  std::vector<bool> index_visited(elements.size(), false);

  for (std::size_t i = 0; i < elements.size(); ++i) {
    if (index_visited[i]) continue;

    std::size_t j = sorting_permutation[i];
    std::string value = elements[j];
    while (j != i) {
      elements[j] = elements[sorting_permutation[j]];
      index_visited[j] = true;
      j = sorting_permutation[j];
    }
    elements[i] = value;
  }
  
  return absl::WrapUnique(new Raw(elements));
}

// CreateFromProtobuf is called by the client when processing the server's
// setup message. It contains the encrypted version of the server's own data.
// These are sorted when the server Raw::Create()s.

StatusOr<std::unique_ptr<Raw>> Raw::CreateFromProtobuf(
    const psi_proto::ServerSetup& encoded_filter) {
  if (!encoded_filter.IsInitialized()) {
    return absl::InvalidArgumentError("`ServerSetup` is corrupt!");
  }

  std::vector<std::string> encrypted_elements(
      encoded_filter.raw().encrypted_elements().begin(),
      encoded_filter.raw().encrypted_elements().end());

  return absl::WrapUnique(new Raw(encrypted_elements));
}

std::pair<std::vector<size_t>, std::vector<size_t>>
Raw::GetAssociationTable(
  std::vector<std::string>& decrypted) const
{
  // decrypted are the server's response, that is the client's own data after
  // the server has encrypted it. They are not yet sorted.
  std::vector<std::size_t> permutation(decrypted.size());

  std::iota(permutation.begin(), permutation.end(), 0);

  std::sort(
    permutation.begin(), permutation.end(),
    [&decrypted](size_t i1, size_t i2) {return decrypted[i1] < decrypted[i2];}
  );

  std::vector<bool> index_visited(decrypted.size(), false);

  for (std::size_t i = 0; i < decrypted.size(); ++i) {
    if (index_visited[i]) continue;

    std::size_t j = permutation[i];
    std::string value = decrypted[j];
    while (j != i) {
      decrypted[j] = decrypted[permutation[j]];
      index_visited[j] = true;
      j = permutation[j];
    }
    decrypted[i] = value;
  }

  std::vector<size_t> decrypted_permutation, encrypted_permutation;

  size_t i = 0, j = 0;

  while (i < decrypted.size() && j < encrypted_.size()) {
    // advance decrypted until it is no longer behind encrypted_
    while (i < decrypted.size() && decrypted[i] < encrypted_[j]) ++i;
    if (i >= decrypted.size()) break;

    // assert decrypted[i] >= encrypted_[j]
    size_t first_j = j;
    std::string first_encrypted = encrypted_[j];
    if (decrypted[i] == encrypted_[j]) while (true) {
      // account for the possibility that multiple decrypted[i] equal
      // multiple encrypted
      do {
        decrypted_permutation.push_back(permutation[i]);
        encrypted_permutation.push_back(j);
        ++j;
      } while (j < encrypted_.size() && decrypted[i] == encrypted_[j]);

      ++i;
      if (i < decrypted.size() && decrypted[i] == first_encrypted) {
        // reset j so we can run it again
        j = first_j;
      } else {
        break;
      }
    }
    // i and j should now have advanced past the point where ecnrypted and
    // decrypted were equal. We advance encrypted until it is past decrypted
    while (j < encrypted_.size() && decrypted[i] > encrypted_[j]) ++j;
  }

  return make_pair(decrypted_permutation, encrypted_permutation);
}

std::vector<int64_t> Raw::Intersect(
    absl::Span<const std::string> elements) const {
  // This implementation creates a copy of `elements`, but the tradeoff is that
  // we can compute the intersection in O(nlog(n) + max(n, m)) where `n` and `m`
  // correspond to the number of client and server elements respectively.
  std::vector<std::pair<std::string, int64_t>> vp(elements.size());

  // Collect a pair with the index to track the original index after sorting.
  for (size_t i = 0; i < elements.size(); ++i) {
    vp[i] = std::make_pair(elements[i], (int64_t)i);
  }

  // Next, we sort the collection. O(nlog(n))
  std::sort(vp.begin(), vp.end());

  std::vector<int64_t> res;
  // Compute intersection. O(max(m, n))
  custom_set_intersection(vp.begin(), vp.end(), encrypted_.begin(),
                          encrypted_.end(), std::back_inserter(res));

  return res;
}

size_t Raw::size() const { return encrypted_.size(); }

psi_proto::ServerSetup Raw::ToProtobuf() const {
  psi_proto::ServerSetup server_setup;
  *server_setup.mutable_raw()->mutable_encrypted_elements() = {
      encrypted_.begin(), encrypted_.end()};

  return server_setup;
}

}  // namespace private_set_intersection
