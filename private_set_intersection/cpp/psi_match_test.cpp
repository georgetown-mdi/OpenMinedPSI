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
#include <cstdint>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/str_cat.h"
#include "gtest/gtest.h"
#include "private_set_intersection/cpp/psi_client.h"
#include "private_set_intersection/cpp/psi_server.h"
#include "private_set_intersection/proto/psi.pb.h"
#include "util/status_matchers.h"

namespace private_set_intersection {
namespace {

using Pairs = std::vector<std::pair<uint64_t, uint64_t>>;

// One round's messages, serialized as they cross the wire.
struct Round {
  std::string setup;
  std::string response;
  psi_proto::ServerSetup setup_proto;
  psi_proto::Response response_proto;
  int64_t response_size = 0;
};

class PsiMatchTest : public ::testing::Test {
 protected:
  void SetUpParties(bool reveal_intersection) {
    PSI_ASSERT_OK_AND_ASSIGN(client_,
                             PsiClient::CreateWithNewKey(reveal_intersection));
    PSI_ASSERT_OK_AND_ASSIGN(server_,
                             PsiServer::CreateWithNewKey(reveal_intersection));
  }

  // A round over `server_count` server inputs and `client_count` client
  // inputs drawn with overlap and with repeats on the client side, so the
  // response holds equal elements.
  void MakeRound(int server_count, int client_count, uint32_t seed,
                 Round* round) {
    std::mt19937 random(seed);
    const int universe = std::max(1, server_count + client_count);
    std::vector<std::string> server_inputs;
    for (int i = 0; i < server_count; ++i) {
      server_inputs.push_back(absl::StrCat("Element ", i));
    }
    std::vector<std::string> client_inputs;
    std::uniform_int_distribution<int> pick(0, universe - 1);
    for (int i = 0; i < client_count; ++i) {
      client_inputs.push_back(absl::StrCat("Element ", pick(random)));
    }
    PSI_ASSERT_OK_AND_ASSIGN(round->setup_proto,
                             server_->CreateSetupMessage(0.0, -1, server_inputs,
                                                         DataStructure::Raw));
    PSI_ASSERT_OK_AND_ASSIGN(auto request,
                             client_->CreateRequest(client_inputs));
    PSI_ASSERT_OK_AND_ASSIGN(round->response_proto,
                             server_->ProcessRequest(request));
    round->setup = round->setup_proto.SerializeAsString();
    round->response = round->response_proto.SerializeAsString();
    round->response_size = round->response_proto.encrypted_elements_size();
  }

  // Runs a match feeding the setup and the response in windows of the given
  // sizes (0: whole).
  absl::StatusOr<PsiMatchResult> RunMatch(const Round& round,
                                          std::size_t setup_window,
                                          std::size_t response_window) {
    ASSIGN_OR_RETURN(auto match, client_->CreateMatch());
    RETURN_IF_ERROR(Feed(round.setup, setup_window, [&](absl::string_view b) {
      return match->AddSetupBytes(b);
    }));
    RETURN_IF_ERROR(match->SealSetup());
    RETURN_IF_ERROR(Feed(
        round.response, response_window,
        [&](absl::string_view b) { return match->MatchResponsePiece(b); }));
    return match->Finish();
  }

  template <typename Add>
  static absl::Status Feed(const std::string& bytes, std::size_t window,
                           Add add) {
    if (window == 0) return add(bytes);
    for (std::size_t at = 0; at < bytes.size(); at += window) {
      RETURN_IF_ERROR(add(absl::string_view(bytes).substr(at, window)));
    }
    return absl::OkStatus();
  }

  static Pairs SortedPairs(const std::vector<std::size_t>& response,
                           const std::vector<std::size_t>& setup) {
    Pairs pairs;
    for (std::size_t i = 0; i < response.size(); ++i) {
      pairs.emplace_back(response[i], setup[i]);
    }
    std::sort(pairs.begin(), pairs.end());
    return pairs;
  }

  static Pairs SortedPairs(const PsiMatchResult& result) {
    return SortedPairs(std::vector<std::size_t>(result.response_indices.begin(),
                                                result.response_indices.end()),
                       std::vector<std::size_t>(result.setup_indices.begin(),
                                                result.setup_indices.end()));
  }

  // A serialized Raw setup holding `elements` in the order given.
  static std::string RawSetup(const std::vector<std::string>& elements) {
    psi_proto::ServerSetup setup;
    for (const std::string& element : elements) {
      setup.mutable_raw()->add_encrypted_elements(element);
    }
    return setup.SerializeAsString();
  }

  // The status a fresh match gives for `setup`, at the first failing call
  // through SealSetup.
  absl::Status SetupStatus(const std::string& setup) {
    absl::StatusOr<std::unique_ptr<PsiMatch>> match = client_->CreateMatch();
    if (!match.ok()) return match.status();
    absl::Status status = (*match)->AddSetupBytes(setup);
    if (!status.ok()) return status;
    return (*match)->SealSetup();
  }

  std::unique_ptr<PsiClient> client_;
  std::unique_ptr<PsiServer> server_;
};

TEST_F(PsiMatchTest, AssociationTableEqualsTheLibrarys) {
  SetUpParties(true);
  const std::vector<std::pair<int, int>> sizes = {
      {0, 0}, {0, 7}, {7, 0}, {1, 1}, {100, 60}, {1000, 1500}, {2500, 400}};
  for (std::size_t s = 0; s < sizes.size(); ++s) {
    SCOPED_TRACE(
        absl::StrCat("setup ", sizes[s].first, ", response ", sizes[s].second));
    Round round;
    MakeRound(sizes[s].first, sizes[s].second, static_cast<uint32_t>(s),
              &round);
    PSI_ASSERT_OK_AND_ASSIGN(
        auto expected,
        client_->GetAssociationTable(round.setup_proto, round.response_proto));
    PSI_ASSERT_OK_AND_ASSIGN(PsiMatchResult result, RunMatch(round, 0, 0));
    EXPECT_EQ(SortedPairs(result),
              SortedPairs(expected.first, expected.second));
    PSI_ASSERT_OK_AND_ASSIGN(
        int64_t expected_size,
        client_->GetIntersectionSize(round.setup_proto, round.response_proto));
    EXPECT_EQ(result.intersection_size, expected_size);
    EXPECT_EQ(result.decrypted_count, round.response_size);
  }
}

TEST_F(PsiMatchTest, IntersectionSizeEqualsTheLibrarys) {
  SetUpParties(false);
  const std::vector<std::pair<int, int>> sizes = {
      {0, 5}, {5, 0}, {100, 60}, {1000, 1500}, {2500, 400}};
  for (std::size_t s = 0; s < sizes.size(); ++s) {
    SCOPED_TRACE(
        absl::StrCat("setup ", sizes[s].first, ", response ", sizes[s].second));
    Round round;
    MakeRound(sizes[s].first, sizes[s].second, static_cast<uint32_t>(100 + s),
              &round);
    PSI_ASSERT_OK_AND_ASSIGN(
        int64_t expected,
        client_->GetIntersectionSize(round.setup_proto, round.response_proto));
    PSI_ASSERT_OK_AND_ASSIGN(PsiMatchResult result, RunMatch(round, 0, 0));
    EXPECT_EQ(result.intersection_size, expected);
    EXPECT_EQ(result.decrypted_count, round.response_size);
    // A count-only match never records which elements matched.
    EXPECT_TRUE(result.response_indices.empty());
    EXPECT_TRUE(result.setup_indices.empty());
  }
}

TEST_F(PsiMatchTest, CountsARepeatedResponseElementOnce) {
  SetUpParties(false);
  Round round;
  MakeRound(50, 0, 7, &round);
  // Every response element equals the first setup element's preimage.
  PSI_ASSERT_OK_AND_ASSIGN(auto request,
                           client_->CreateRequest(std::vector<std::string>(
                               40, std::string("Element 3"))));
  PSI_ASSERT_OK_AND_ASSIGN(round.response_proto,
                           server_->ProcessRequest(request));
  round.response = round.response_proto.SerializeAsString();
  // Fed one element per piece, as a sum over pieces would count it 40 times.
  PSI_ASSERT_OK_AND_ASSIGN(PsiMatchResult result, RunMatch(round, 0, 35));
  EXPECT_EQ(result.intersection_size, 1);
  PSI_ASSERT_OK_AND_ASSIGN(
      int64_t expected,
      client_->GetIntersectionSize(round.setup_proto, round.response_proto));
  EXPECT_EQ(result.intersection_size, expected);
  EXPECT_EQ(result.decrypted_count, 40);
}

TEST_F(PsiMatchTest, ResultDoesNotDependOnHowTheBytesAreCut) {
  SetUpParties(true);
  Round round;
  MakeRound(300, 200, 11, &round);
  PSI_ASSERT_OK_AND_ASSIGN(PsiMatchResult whole, RunMatch(round, 0, 0));
  ASSERT_GT(whole.intersection_size, 0);
  // Every element is 33 bytes framed by a tag and a one-byte length, so 35
  // cuts at element edges and the others cut through elements and framing.
  for (std::size_t window :
       {std::size_t{1}, std::size_t{2}, std::size_t{34}, std::size_t{35},
        std::size_t{36}, std::size_t{35 * 64}, std::size_t{1000}}) {
    SCOPED_TRACE(absl::StrCat("window ", window));
    PSI_ASSERT_OK_AND_ASSIGN(PsiMatchResult result,
                             RunMatch(round, window, window));
    EXPECT_EQ(result.response_indices, whole.response_indices);
    EXPECT_EQ(result.setup_indices, whole.setup_indices);
    EXPECT_EQ(result.intersection_size, whole.intersection_size);
    EXPECT_EQ(result.decrypted_count, round.response_size);
  }
}

TEST_F(PsiMatchTest, AcceptsEmptyPieces) {
  SetUpParties(true);
  Round round;
  MakeRound(20, 20, 13, &round);
  PSI_ASSERT_OK_AND_ASSIGN(auto match, client_->CreateMatch());
  ASSERT_TRUE(match->AddSetupBytes("").ok());
  ASSERT_TRUE(match->AddSetupBytes(round.setup).ok());
  ASSERT_TRUE(match->AddSetupBytes("").ok());
  ASSERT_TRUE(match->SealSetup().ok());
  ASSERT_TRUE(match->MatchResponsePiece("").ok());
  ASSERT_TRUE(match->MatchResponsePiece(round.response).ok());
  PSI_ASSERT_OK_AND_ASSIGN(PsiMatchResult result, match->Finish());
  EXPECT_EQ(result.decrypted_count, 20);
}

TEST_F(PsiMatchTest, DecryptsAResponseLargerThanOneBatchOnce) {
  SetUpParties(true);
  Round round;
  MakeRound(1000, (1 << 16) + 1000, 17, &round);
  PSI_ASSERT_OK_AND_ASSIGN(
      auto expected,
      client_->GetAssociationTable(round.setup_proto, round.response_proto));
  PSI_ASSERT_OK_AND_ASSIGN(PsiMatchResult result, RunMatch(round, 0, 0));
  EXPECT_EQ(result.decrypted_count, round.response_size);
  EXPECT_EQ(SortedPairs(result), SortedPairs(expected.first, expected.second));
}

TEST_F(PsiMatchTest, ProgressCountsEachDecryptedElement) {
  SetUpParties(false);
  Round round;
  MakeRound(10, 300, 19, &round);
  PSI_ASSERT_OK_AND_ASSIGN(auto match, client_->CreateMatch());
  ASSERT_TRUE(match->AddSetupBytes(round.setup).ok());
  ASSERT_TRUE(match->SealSetup().ok());
  int32_t progress = 0;
  const std::size_t half = 35 * 100 + 17;
  ASSERT_TRUE(
      match
          ->MatchResponsePiece(
              absl::string_view(round.response).substr(0, half), &progress)
          .ok());
  EXPECT_EQ(progress, 100);
  ASSERT_TRUE(match
                  ->MatchResponsePiece(
                      absl::string_view(round.response).substr(half), &progress)
                  .ok());
  EXPECT_EQ(progress, 300);
}

TEST_F(PsiMatchTest, RefusesASetupThatIsNotStrictlyAscending) {
  SetUpParties(true);
  Round round;
  MakeRound(3, 0, 23, &round);
  std::vector<std::string> elements(
      round.setup_proto.raw().encrypted_elements().begin(),
      round.setup_proto.raw().encrypted_elements().end());
  ASSERT_TRUE(SetupStatus(RawSetup(elements)).ok());

  std::vector<std::string> swapped = elements;
  std::swap(swapped[1], swapped[2]);
  absl::Status status = SetupStatus(RawSetup(swapped));
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_NE(status.message().find("strictly ascending"), std::string::npos);

  std::vector<std::string> repeated = {elements[0], elements[1], elements[1]};
  EXPECT_EQ(SetupStatus(RawSetup(repeated)).code(),
            absl::StatusCode::kInvalidArgument);

  // Unsigned order: 0x80 sorts after 0x7F.
  EXPECT_TRUE(SetupStatus(RawSetup({"\x7F", "\x80"})).ok());
  EXPECT_FALSE(SetupStatus(RawSetup({"\x80", "\x7F"})).ok());
  // A prefix sorts first.
  EXPECT_TRUE(SetupStatus(RawSetup({"ab", "abc"})).ok());
  EXPECT_FALSE(SetupStatus(RawSetup({"abc", "ab"})).ok());
}

TEST_F(PsiMatchTest, RefusesAWrongTag) {
  SetUpParties(true);
  // A GCS setup: field 2 in place of `raw`.
  PSI_ASSERT_OK_AND_ASSIGN(
      auto gcs,
      server_->CreateSetupMessage(0.01, 10, std::vector<std::string>{"a", "b"},
                                  DataStructure::Gcs));
  absl::Status status = SetupStatus(gcs.SerializeAsString());
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  // An unknown field inside `raw`.
  std::string inner = std::string(
      "\x0A\x01"
      "a"
      "\x12\x01"
      "b",
      6);
  EXPECT_EQ(SetupStatus(std::string("\x0A\x06", 2) + inner).code(),
            absl::StatusCode::kInvalidArgument);
  // `raw` with the wrong wire type.
  EXPECT_FALSE(SetupStatus(std::string("\x08\x01", 2)).ok());
  // An unknown field in the response.
  PSI_ASSERT_OK_AND_ASSIGN(auto match, client_->CreateMatch());
  ASSERT_TRUE(match->AddSetupBytes(RawSetup({"a"})).ok());
  ASSERT_TRUE(match->SealSetup().ok());
  status =
      match->MatchResponsePiece(std::string("\x12\x01"
                                            "a",
                                            3));
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
}

TEST_F(PsiMatchTest, RefusesTrailingBytes) {
  SetUpParties(true);
  const std::string setup = RawSetup({"a", "b"});
  ASSERT_TRUE(SetupStatus(setup).ok());
  EXPECT_EQ(SetupStatus(setup + std::string("\x0A\x00", 2)).code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(SetupStatus(setup + "x").code(),
            absl::StatusCode::kInvalidArgument);
  // Trailing bytes cut into their own window.
  PSI_ASSERT_OK_AND_ASSIGN(auto match, client_->CreateMatch());
  ASSERT_TRUE(match->AddSetupBytes(setup).ok());
  EXPECT_EQ(match->AddSetupBytes("x").code(),
            absl::StatusCode::kInvalidArgument);
}

TEST_F(PsiMatchTest, RefusesAnIncompleteSetupOrResponse) {
  SetUpParties(true);
  Round round;
  MakeRound(5, 5, 29, &round);
  EXPECT_EQ(SetupStatus("").code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(SetupStatus(round.setup.substr(0, round.setup.size() - 1)).code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(SetupStatus(round.setup.substr(0, 1)).code(),
            absl::StatusCode::kInvalidArgument);
  // An element whose length runs past the end of `raw`.
  EXPECT_EQ(SetupStatus(std::string("\x0A\x03\x0A\x05"
                                    "a",
                                    5))
                .code(),
            absl::StatusCode::kInvalidArgument);

  PSI_ASSERT_OK_AND_ASSIGN(auto match, client_->CreateMatch());
  ASSERT_TRUE(match->AddSetupBytes(round.setup).ok());
  ASSERT_TRUE(match->SealSetup().ok());
  ASSERT_TRUE(
      match
          ->MatchResponsePiece(absl::string_view(round.response)
                                   .substr(0, round.response.size() - 1))
          .ok());
  EXPECT_EQ(match->Finish().status().code(),
            absl::StatusCode::kInvalidArgument);
}

TEST_F(PsiMatchTest, RefusesAnOverLengthEntry) {
  SetUpParties(true);
  const std::string longest(PsiMatch::kMaxElementBytes, 'a');
  EXPECT_TRUE(SetupStatus(RawSetup({longest})).ok());
  const std::string over(PsiMatch::kMaxElementBytes + 1, 'a');
  absl::Status status = SetupStatus(RawSetup({over}));
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_NE(status.message().find("longer than"), std::string::npos);
  // A length too large for 32 bits.
  EXPECT_EQ(SetupStatus(std::string("\x0A\xFF\xFF\xFF\xFF\x1F", 6)).code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(SetupStatus(std::string("\x0A\x80\x80\x80\x80\x80\x01", 7)).code(),
            absl::StatusCode::kInvalidArgument);

  PSI_ASSERT_OK_AND_ASSIGN(auto match, client_->CreateMatch());
  ASSERT_TRUE(match->AddSetupBytes(RawSetup({"a"})).ok());
  ASSERT_TRUE(match->SealSetup().ok());
  psi_proto::Response response;
  response.add_encrypted_elements(over);
  EXPECT_EQ(match->MatchResponsePiece(response.SerializeAsString()).code(),
            absl::StatusCode::kInvalidArgument);
}

TEST_F(PsiMatchTest, RefusesAResponseElementThatDoesNotDecrypt) {
  SetUpParties(true);
  PSI_ASSERT_OK_AND_ASSIGN(auto match, client_->CreateMatch());
  ASSERT_TRUE(match->AddSetupBytes(RawSetup({"a"})).ok());
  ASSERT_TRUE(match->SealSetup().ok());
  psi_proto::Response response;
  response.add_encrypted_elements(std::string(33, '\x07'));
  EXPECT_FALSE(match->MatchResponsePiece(response.SerializeAsString()).ok());
  EXPECT_FALSE(match->Finish().ok());
}

TEST_F(PsiMatchTest, RefusesCallsOutOfOrder) {
  SetUpParties(true);
  Round round;
  MakeRound(5, 5, 31, &round);
  {
    PSI_ASSERT_OK_AND_ASSIGN(auto match, client_->CreateMatch());
    EXPECT_EQ(match->MatchResponsePiece(round.response).code(),
              absl::StatusCode::kFailedPrecondition);
  }
  {
    PSI_ASSERT_OK_AND_ASSIGN(auto match, client_->CreateMatch());
    EXPECT_EQ(match->Finish().status().code(),
              absl::StatusCode::kFailedPrecondition);
  }
  {
    // Setup bytes after the seal.
    PSI_ASSERT_OK_AND_ASSIGN(auto match, client_->CreateMatch());
    ASSERT_TRUE(match->AddSetupBytes(round.setup).ok());
    ASSERT_TRUE(match->SealSetup().ok());
    EXPECT_EQ(match->AddSetupBytes("").code(),
              absl::StatusCode::kFailedPrecondition);
  }
  {
    PSI_ASSERT_OK_AND_ASSIGN(auto match, client_->CreateMatch());
    ASSERT_TRUE(match->AddSetupBytes(round.setup).ok());
    ASSERT_TRUE(match->SealSetup().ok());
    EXPECT_EQ(match->SealSetup().code(), absl::StatusCode::kFailedPrecondition);
  }
  {
    // A piece, or a second Finish, after Finish.
    PSI_ASSERT_OK_AND_ASSIGN(auto match, client_->CreateMatch());
    ASSERT_TRUE(match->AddSetupBytes(round.setup).ok());
    ASSERT_TRUE(match->SealSetup().ok());
    ASSERT_TRUE(match->MatchResponsePiece(round.response).ok());
    ASSERT_TRUE(match->Finish().ok());
    EXPECT_EQ(match->MatchResponsePiece(round.response).code(),
              absl::StatusCode::kFailedPrecondition);
  }
  {
    PSI_ASSERT_OK_AND_ASSIGN(auto match, client_->CreateMatch());
    ASSERT_TRUE(match->AddSetupBytes(round.setup).ok());
    ASSERT_TRUE(match->SealSetup().ok());
    ASSERT_TRUE(match->Finish().ok());
    EXPECT_EQ(match->Finish().status().code(),
              absl::StatusCode::kFailedPrecondition);
  }
}

TEST_F(PsiMatchTest, KeepsRefusingAfterARefusal) {
  SetUpParties(true);
  Round round;
  MakeRound(5, 5, 37, &round);
  PSI_ASSERT_OK_AND_ASSIGN(auto match, client_->CreateMatch());
  const absl::Status refused = match->AddSetupBytes("\x12");
  ASSERT_EQ(refused.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(match->AddSetupBytes(round.setup), refused);
  EXPECT_EQ(match->SealSetup(), refused);
  EXPECT_EQ(match->MatchResponsePiece(round.response), refused);
  EXPECT_EQ(match->Finish().status(), refused);
}

TEST_F(PsiMatchTest, OutlivesTheClient) {
  SetUpParties(true);
  Round round;
  MakeRound(100, 100, 41, &round);
  PSI_ASSERT_OK_AND_ASSIGN(
      auto expected,
      client_->GetAssociationTable(round.setup_proto, round.response_proto));
  PSI_ASSERT_OK_AND_ASSIGN(auto match, client_->CreateMatch());
  client_.reset();
  ASSERT_TRUE(match->AddSetupBytes(round.setup).ok());
  ASSERT_TRUE(match->SealSetup().ok());
  ASSERT_TRUE(match->MatchResponsePiece(round.response).ok());
  PSI_ASSERT_OK_AND_ASSIGN(PsiMatchResult result, match->Finish());
  EXPECT_EQ(SortedPairs(result), SortedPairs(expected.first, expected.second));
}

}  // namespace
}  // namespace private_set_intersection
