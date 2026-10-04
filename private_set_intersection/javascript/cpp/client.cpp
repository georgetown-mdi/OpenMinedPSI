#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "emscripten/bind.h"
#include "private_set_intersection/cpp/psi_match.h"
#include "private_set_intersection/javascript/cpp/utils.h"
#include "private_set_intersection/proto/psi.pb.h"
#include "psi_client.h"

namespace {

// The most of a JS byte array copied into linear memory at once, so a large
// setup or response never needs a copy of its own size.
constexpr std::size_t kWindowBytes = std::size_t{1} << 20;

// Hands the bytes of the Uint8Array `bytes` to `consume` in windows of at most
// kWindowBytes, each copied with one TypedArray.set. An empty array is handed
// over as one empty window, so `consume` still runs its call-order checks.
template <typename Consume>
absl::Status ForEachWindow(const emscripten::val& bytes, Consume consume) {
  const std::size_t length = bytes["length"].as<std::size_t>();
  if (length == 0) return consume(absl::string_view());
  std::vector<std::uint8_t> window(std::min(length, kWindowBytes));
  for (std::size_t at = 0; at < length; at += window.size()) {
    const std::size_t size = std::min(window.size(), length - at);
    // A fresh view each time: growing the heap detaches the previous one.
    emscripten::val(emscripten::typed_memory_view(size, window.data()))
        .call<void>("set",
                    bytes.call<emscripten::val>("subarray", at, at + size));
    absl::Status status = consume(
        absl::string_view(reinterpret_cast<const char*>(window.data()), size));
    if (!status.ok()) return status;
  }
  return absl::OkStatus();
}

// A JS-owned Uint32Array copy of `values`.
emscripten::val ToUint32Array(const std::vector<std::uint32_t>& values) {
  return emscripten::val::global("Uint32Array")
      .new_(emscripten::typed_memory_view(values.size(), values.data()));
}

}  // namespace

EMSCRIPTEN_BINDINGS(PSI_Match) {
  using emscripten::optional_override;
  using private_set_intersection::ProgressPointer;
  using private_set_intersection::PsiMatch;
  using private_set_intersection::PsiMatchResult;
  using private_set_intersection::ToJSStatus;

  emscripten::class_<PsiMatch>("PsiMatch")
      .smart_ptr<std::shared_ptr<PsiMatch>>("std::shared_ptr<PsiMatch>")
      .function(
          "AddSetupBytes",
          optional_override([](PsiMatch& self, const emscripten::val& bytes) {
            return ToJSStatus(
                ForEachWindow(bytes, [&](absl::string_view window) {
                  return self.AddSetupBytes(window);
                }));
          }))
      .function("SealSetup", optional_override([](PsiMatch& self) {
                  return ToJSStatus(self.SealSetup());
                }))
      .function(
          "MatchResponsePiece",
          optional_override([](PsiMatch& self, const emscripten::val& bytes,
                               const emscripten::val& progress_ptr) {
            std::int32_t* progress = ProgressPointer(progress_ptr);
            return ToJSStatus(
                ForEachWindow(bytes, [&](absl::string_view window) {
                  return self.MatchResponsePiece(window, progress);
                }));
          }))
      .function("Finish", optional_override([](PsiMatch& self) {
                  absl::StatusOr<PsiMatchResult> finished = self.Finish();
                  if (!finished.ok()) return ToJSStatus(finished.status());
                  auto value = emscripten::val::object();
                  value.set("IntersectionSize",
                            static_cast<double>(finished->intersection_size));
                  value.set("DecryptedCount",
                            static_cast<double>(finished->decrypted_count));
                  if (self.reveal_intersection()) {
                    value.set("ResponseIndices",
                              ToUint32Array(finished->response_indices));
                    value.set("SetupIndices",
                              ToUint32Array(finished->setup_indices));
                  } else {
                    value.set("ResponseIndices", emscripten::val::null());
                    value.set("SetupIndices", emscripten::val::null());
                  }
                  auto result = emscripten::val::object();
                  result.set("Value", value);
                  result.set("Status", emscripten::val::null());
                  return result;
                }));
}

EMSCRIPTEN_BINDINGS(PSI_Client) {
  using absl::StatusOr;
  using emscripten::optional_override;
  using private_set_intersection::ProgressPointer;
  using private_set_intersection::PsiClient;
  using private_set_intersection::ToJSObject;
  using private_set_intersection::ToSerializedJSObject;
  using private_set_intersection::ToShared;

  emscripten::class_<PsiClient>("PsiClient")
      .smart_ptr<std::shared_ptr<PsiClient>>("std::shared_ptr<PsiClient>")
      .class_function(
          "CreateWithNewKey", optional_override([](bool reveal_intersection) {
            return ToJSObject(
                ToShared(PsiClient::CreateWithNewKey(reveal_intersection)));
          }))
      .class_function("CreateFromKey",
                      optional_override([](const emscripten::val& byte_array,
                                           bool reveal_intersection) {
                        const std::size_t l =
                            byte_array["length"].as<std::size_t>();
                        std::string byte_string(l, '\0');

                        for (std::size_t i = 0; i < l; i++) {
                          byte_string[i] = byte_array[i].as<std::uint8_t>();
                        }

                        return ToJSObject(ToShared(PsiClient::CreateFromKey(
                            byte_string, reveal_intersection)));
                      }))
      .function("CreateRequest",
                optional_override([](const PsiClient& self,
                                     const emscripten::val& byte_array,
                                     const emscripten::val& progress_ptr) {
                  std::vector<std::string> string_vector;
                  const std::size_t l = byte_array["length"].as<std::size_t>();
                  string_vector.reserve(l);

                  for (std::size_t i = 0; i < l; ++i) {
                    string_vector.push_back(byte_array[i].as<std::string>());
                  }

                  StatusOr<psi_proto::Request> request;
                  auto status = self.CreateRequest(
                      string_vector, ProgressPointer(progress_ptr));
                  if (status.ok()) {
                    request = *status;
                  } else {
                    request = status.status();
                  }
                  return ToSerializedJSObject(request);
                }))
      .function(
          "GetIntersection",
          optional_override([](const PsiClient& self,
                               const emscripten::val& server_setup_array,
                               const emscripten::val& server_response_array,
                               const emscripten::val& progress_ptr) {
            const std::size_t server_setup_length =
                server_setup_array["length"].as<std::size_t>();
            std::string server_setup_string(server_setup_length, '\0');
            for (std::size_t i = 0; i < server_setup_length; i++) {
              server_setup_string[i] = server_setup_array[i].as<std::uint8_t>();
            }

            const std::size_t server_response_length =
                server_response_array["length"].as<std::size_t>();
            std::string server_response_string(server_response_length, '\0');
            for (std::size_t i = 0; i < server_response_length; i++) {
              server_response_string[i] =
                  server_response_array[i].as<std::uint8_t>();
            }

            psi_proto::ServerSetup server_setup;
            server_setup.ParseFromString(server_setup_string);
            psi_proto::Response server_response;
            server_response.ParseFromString(server_response_string);

            // We need to convert to a JS array explicitly because JS
            // doesn't know about vector<int64_t>.
            StatusOr<emscripten::val> result;
            const auto status = self.GetIntersection(
                server_setup, server_response, ProgressPointer(progress_ptr));
            if (status.ok()) {
              // Convert int64_t to int32_t for JS
              const std::vector<std::int64_t> unsupported_result = *status;
              const std::vector<std::int32_t> supported_result(
                  unsupported_result.begin(), unsupported_result.end());
              // Convert vector to JS array
              emscripten::val array = emscripten::val::array(
                  supported_result.begin(), supported_result.end());
              result = StatusOr<emscripten::val>(array);
            } else {
              result = status.status();
            }
            return ToJSObject(result);
          }))
      .function(
          "GetAssociationTable",
          optional_override([](const PsiClient& self,
                               const emscripten::val& server_setup_array,
                               const emscripten::val& server_response_array,
                               const emscripten::val& progress_ptr) {
            const std::size_t server_setup_length =
                server_setup_array["length"].as<std::size_t>();
            std::string server_setup_string(server_setup_length, '\0');
            for (std::size_t i = 0; i < server_setup_length; i++) {
              server_setup_string[i] = server_setup_array[i].as<std::uint8_t>();
            }

            const std::size_t server_response_length =
                server_response_array["length"].as<std::size_t>();
            std::string server_response_string(server_response_length, '\0');
            for (std::size_t i = 0; i < server_response_length; i++) {
              server_response_string[i] =
                  server_response_array[i].as<std::uint8_t>();
            }

            psi_proto::ServerSetup server_setup;
            server_setup.ParseFromString(server_setup_string);
            psi_proto::Response server_response;
            server_response.ParseFromString(server_response_string);

            // We need to convert to a JS array explicitly because JS
            // doesn't know about vector<int64_t>.
            StatusOr<emscripten::val> result;
            const auto status = self.GetAssociationTable(
                server_setup, server_response, ProgressPointer(progress_ptr));
            if (status.ok()) {
              // Convert int64_t to int32_t for JS
              const std::pair<std::vector<std::size_t>,
                              std::vector<std::size_t>>
                  unsupported_result = *status;
              std::vector<std::int32_t> first_result(
                  unsupported_result.first.begin(),
                  unsupported_result.first.end());
              std::vector<std::int32_t> second_result(
                  unsupported_result.second.begin(),
                  unsupported_result.second.end());

              // Convert vector to JS array
              std::vector<emscripten::val> supported_result(2);
              supported_result[0] = emscripten::val::array(first_result.begin(),
                                                           first_result.end());
              supported_result[1] = emscripten::val::array(
                  second_result.begin(), second_result.end());
              emscripten::val array = emscripten::val::array(
                  supported_result.begin(), supported_result.end());

              result = StatusOr<emscripten::val>(array);
            } else {
              result = status.status();
            }
            return ToJSObject(result);
          }))
      .function(
          "GetIntersectionSize",
          optional_override([](const PsiClient& self,
                               const emscripten::val& server_setup_array,
                               const emscripten::val& server_response_array,
                               const emscripten::val& progress_ptr) {
            const std::size_t server_setup_length =
                server_setup_array["length"].as<std::size_t>();
            std::string server_setup_string(server_setup_length, '\0');
            for (std::size_t i = 0; i < server_setup_length; i++) {
              server_setup_string[i] = server_setup_array[i].as<std::uint8_t>();
            }

            const std::size_t server_response_length =
                server_response_array["length"].as<std::size_t>();
            std::string server_response_string(server_response_length, '\0');
            for (std::size_t i = 0; i < server_response_length; i++) {
              server_response_string[i] =
                  server_response_array[i].as<std::uint8_t>();
            }

            psi_proto::ServerSetup server_setup;
            server_setup.ParseFromString(server_setup_string);
            psi_proto::Response server_response;
            server_response.ParseFromString(server_response_string);

            // We need to convert to an int32 explicitly because JS
            // doesn't have 64-bit integers.
            StatusOr<uint32_t> result;
            const auto status = self.GetIntersectionSize(
                server_setup, server_response, ProgressPointer(progress_ptr));
            if (status.ok()) {
              result = *status;
            } else {
              result = status.status();
            }
            return ToJSObject(result);
          }))
      .function("CreateMatch", optional_override([](const PsiClient& self) {
                  return ToJSObject(ToShared(self.CreateMatch()));
                }))
      .function(
          "GetPrivateKeyBytes", optional_override([](const PsiClient& self) {
            const std::string byte_string = self.GetPrivateKeyBytes();
            const std::vector<std::uint8_t> byte_vector(byte_string.begin(),
                                                        byte_string.end());
            emscripten::val byte_array =
                emscripten::val::array(byte_vector.begin(), byte_vector.end());
            return byte_array;
          }));
}