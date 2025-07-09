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

#ifndef PRIVATE_SET_INTERSECTION_CPP_RAW_H_
#define PRIVATE_SET_INTERSECTION_CPP_RAW_H_

#include <cstddef> // std::size_t
#include <memory>  // unique_ptr
#include <utility> // std::pair
#include <string>  // std::string
#include <vector>  // std::vector

#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "private_join_and_compute/crypto/context.h"
#include "private_set_intersection/proto/psi.pb.h"

namespace private_set_intersection {

using absl::StatusOr;

// A Raw datastructure is a simple container for holding raw encrypted values.
class Raw {
 public:
  Raw() = delete;

  static StatusOr<std::unique_ptr<Raw>> Create(
    std::vector<std::string> elements,
    std::vector<std::size_t>* sorting_permutation = nullptr
  );

  // Creates a container containing holding encrypted values from a protocol
  // buffer
  static StatusOr<std::unique_ptr<Raw>> CreateFromProtobuf(
      const psi_proto::ServerSetup& encoded_filter);

  // Calculates the intersection
  std::vector<int64_t> Intersect(absl::Span<const std::string> elements) const;

  // Calculates associative table mapping encrypted to decrypted
  std::pair<std::vector<std::size_t>, std::vector<std::size_t>>
  GetAssociationTable(std::vector<std::string>& decrypted) const;

  // Returns the size of the encrypted elements
  std::size_t size() const;

  // Returns a protobuf representation of the container
  psi_proto::ServerSetup ToProtobuf() const;

 private:
  Raw(std::vector<std::string> encrypted);
  const std::vector<std::string> encrypted_;
};

}  // namespace private_set_intersection

#endif  // PRIVATE_SET_INTERSECTION_CPP_RAW_H_
