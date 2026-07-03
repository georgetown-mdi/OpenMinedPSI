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

// Node-API (N-API) binding for the PSI engine.
//
// This is the native counterpart to the emscripten/WASM binding in
// private_set_intersection/javascript/cpp/. It wraps the SAME C++ PsiServer /
// PsiClient over the SAME BoringSSL P-256 ECCommutativeCipher and the SAME
// protobuf wire format, so a setup / request / response produced here is
// byte-for-byte identical to the WASM build (the psi-engine-wire-vectors.json
// fixture in @psilink/core pins that contract).
//
// It deliberately reproduces the raw-module surface the emscripten Module
// exposes -- a `Library` object with `PsiServer` / `PsiClient` factories,
// `DataStructure`, and `Package`, whose operations return the
// `{ Value, Status, Permutation? }` shape -- so the existing TypeScript
// implementation/ wrapper layer consumes it unchanged. See
// private_set_intersection/javascript/custom_types/psi/index.d.ts for that
// contract and javascript/cpp/utils.h for the {Value, Status} convention.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "napi.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "private_set_intersection/cpp/datastructure/datastructure.h"
#include "private_set_intersection/cpp/package.h"
#include "private_set_intersection/cpp/psi_client.h"
#include "private_set_intersection/cpp/psi_server.h"
#include "private_set_intersection/proto/psi.pb.h"

namespace {

using ::private_set_intersection::DataStructure;
using ::private_set_intersection::Package;
using ::private_set_intersection::PsiClient;
using ::private_set_intersection::PsiServer;

// ---------------------------------------------------------------------------
// Result helpers -- mirror javascript/cpp/utils.h.
//
// Each fallible operation returns { Value, Status } where exactly one of the
// two is non-null. The TypeScript wrapper checks `if (Status) throw
// Status.Message`, so success must leave `Status` as JS null.
// ---------------------------------------------------------------------------

Napi::Value MakeOk(Napi::Env env, Napi::Value value) {
  Napi::Object result = Napi::Object::New(env);
  result.Set("Value", value);
  result.Set("Status", env.Null());
  return result;
}

Napi::Value MakeError(Napi::Env env, const absl::Status& status) {
  Napi::Object result = Napi::Object::New(env);
  result.Set("Value", env.Null());
  Napi::Object wire_status = Napi::Object::New(env);
  wire_status.Set("StatusCode",
                  Napi::Number::New(env, static_cast<int>(status.raw_code())));
  wire_status.Set("Message",
                  Napi::String::New(env, std::string(status.message())));
  result.Set("Status", wire_status);
  return result;
}

// A fresh Uint8Array owning `bytes.size()` bytes copied from `bytes`.
Napi::Uint8Array ToUint8Array(Napi::Env env, const std::string& bytes) {
  Napi::Uint8Array out = Napi::Uint8Array::New(env, bytes.size());
  if (!bytes.empty()) {
    std::memcpy(out.Data(), bytes.data(), bytes.size());
  }
  return out;
}

// Serialize a protobuf into a fresh Uint8Array wrapped as { Value, Status }.
// The bytes are returned as a Uint8Array (not a boxed JS number array) so a
// multi-MB message stays a single contiguous buffer that protobuf-js consumes
// directly -- see the rationale in javascript/cpp/utils.h::ToSerializedJSObject.
template <typename Proto>
Napi::Value SerializedOk(Napi::Env env, const Proto& proto) {
  const size_t size = proto.ByteSizeLong();
  Napi::Uint8Array bytes = Napi::Uint8Array::New(env, size);
  proto.SerializeToArray(bytes.Data(), static_cast<int>(size));
  return MakeOk(env, bytes);
}

std::vector<std::string> ToStringVector(const Napi::Array& array) {
  const uint32_t length = array.Length();
  std::vector<std::string> out;
  out.reserve(length);
  for (uint32_t i = 0; i < length; ++i) {
    out.push_back(array.Get(i).As<Napi::String>().Utf8Value());
  }
  return out;
}

// Copy the bytes of a Uint8Array / Buffer argument into a std::string. The
// TypeScript wrapper always passes `proto.serializeBinary()` (a Uint8Array).
std::string ToByteString(const Napi::Value& value) {
  Napi::Uint8Array array = value.As<Napi::Uint8Array>();
  return std::string(reinterpret_cast<const char*>(array.Data()),
                     array.ByteLength());
}

template <typename T>
Napi::Array ToNumberArray(Napi::Env env, const std::vector<T>& values) {
  Napi::Array out = Napi::Array::New(env, values.size());
  for (size_t i = 0; i < values.size(); ++i) {
    out.Set(static_cast<uint32_t>(i),
            Napi::Number::New(env, static_cast<double>(values[i])));
  }
  return out;
}

// The int32 slot backing an optional Int32Array argument, into which an
// operation publishes its running processed-element count, or nullptr when the
// argument is absent/nullish. Callers typically back it with a SharedArrayBuffer
// and poll it from another thread (a worker) while the op runs. Only the first
// element is used.
int32_t* ProgressSlot(const Napi::CallbackInfo& info, size_t index) {
  if (info.Length() <= index) return nullptr;
  const Napi::Value value = info[index];
  // Absent or the wrong kind of argument means "no progress reporting"; never
  // throw a TypeError from a cast (e.g. a caller passing the WASM-style numeric
  // offset instead of an Int32Array).
  if (!value.IsTypedArray()) return nullptr;
  Napi::TypedArray typed_array = value.As<Napi::TypedArray>();
  if (typed_array.TypedArrayType() != napi_int32_array) return nullptr;
  Napi::Int32Array array = typed_array.As<Napi::Int32Array>();
  return array.ElementLength() > 0 ? array.Data() : nullptr;
}

// Guards an instance method against being called after delete() zeroed the
// wrapped instance: schedules a JS exception and returns true when the instance
// is gone, so the caller returns instead of dereferencing null (which would
// crash the process). The TypeScript wrapper tracks deletion and never reaches
// this; it only fires on direct misuse of the raw addon.
bool ThrowIfDeleted(Napi::Env env, const void* instance, const char* what) {
  if (instance != nullptr) return false;
  Napi::Error::New(env, std::string(what) + " has been deleted")
      .ThrowAsJavaScriptException();
  return true;
}

// ---------------------------------------------------------------------------
// Per-environment addon state.
//
// The PsiServer / PsiClient class constructors are stored per-Env (via
// SetInstanceData) rather than in file-scope statics so the addon is
// context-aware: a Node worker_thread that loads the addon gets its own
// constructors and the addon can be safely unloaded.
// ---------------------------------------------------------------------------

class PsiServerWrap;
class PsiClientWrap;

struct AddonData {
  Napi::FunctionReference server_ctor;
  Napi::FunctionReference client_ctor;
};

// ---------------------------------------------------------------------------
// PsiServerWrap
// ---------------------------------------------------------------------------

class PsiServerWrap : public Napi::ObjectWrap<PsiServerWrap> {
 public:
  // The class constructor is only ever invoked internally (via an External
  // carrying an already-built PsiServer); it is never exposed to JS.
  static Napi::Function DefineConstructor(Napi::Env env);
  // The { CreateWithNewKey, CreateFromKey } factory object surfaced as
  // `library.PsiServer`.
  static Napi::Object MakeFactory(Napi::Env env);

  explicit PsiServerWrap(const Napi::CallbackInfo& info);

 private:
  static Napi::Value CreateWithNewKey(const Napi::CallbackInfo& info);
  static Napi::Value CreateFromKey(const Napi::CallbackInfo& info);
  static Napi::Value Wrap(
      Napi::Env env, absl::StatusOr<std::unique_ptr<PsiServer>> server);

  Napi::Value CreateSetupMessage(const Napi::CallbackInfo& info);
  Napi::Value ProcessRequest(const Napi::CallbackInfo& info);
  Napi::Value GetPrivateKeyBytes(const Napi::CallbackInfo& info);
  Napi::Value Delete(const Napi::CallbackInfo& info);

  std::unique_ptr<PsiServer> server_;
};

Napi::Function PsiServerWrap::DefineConstructor(Napi::Env env) {
  return DefineClass(
      env, "PsiServer",
      {
          InstanceMethod("CreateSetupMessage",
                         &PsiServerWrap::CreateSetupMessage),
          InstanceMethod("ProcessRequest", &PsiServerWrap::ProcessRequest),
          InstanceMethod("GetPrivateKeyBytes",
                         &PsiServerWrap::GetPrivateKeyBytes),
          InstanceMethod("delete", &PsiServerWrap::Delete),
      });
}

Napi::Object PsiServerWrap::MakeFactory(Napi::Env env) {
  Napi::Object factory = Napi::Object::New(env);
  factory.Set("CreateWithNewKey",
              Napi::Function::New(env, &PsiServerWrap::CreateWithNewKey));
  factory.Set("CreateFromKey",
              Napi::Function::New(env, &PsiServerWrap::CreateFromKey));
  return factory;
}

PsiServerWrap::PsiServerWrap(const Napi::CallbackInfo& info)
    : Napi::ObjectWrap<PsiServerWrap>(info) {
  // info[0] is an External carrying a heap unique_ptr built by Wrap(); move the
  // instance out of it, leaving it null so the External's finalizer is a no-op.
  server_ = std::move(
      *info[0].As<Napi::External<std::unique_ptr<PsiServer>>>().Data());
}

Napi::Value PsiServerWrap::Wrap(
    Napi::Env env, absl::StatusOr<std::unique_ptr<PsiServer>> server) {
  if (!server.ok()) {
    return MakeError(env, server.status());
  }
  AddonData* data = env.GetInstanceData<AddonData>();
  // The instance is owned by a heap unique_ptr carried by the External, whose
  // finalizer frees it. This way it is reclaimed even if constructor.New()
  // throws (e.g. OOM) before the wrapper takes ownership; on the success path
  // the constructor moves out of it, so the finalizer frees a null unique_ptr.
  auto* owned = new std::unique_ptr<PsiServer>(std::move(*server));
  Napi::External<std::unique_ptr<PsiServer>> external =
      Napi::External<std::unique_ptr<PsiServer>>::New(
          env, owned,
          [](Napi::Env, std::unique_ptr<PsiServer>* held) { delete held; });
  Napi::Object instance = data->server_ctor.New({external});
  return MakeOk(env, instance);
}

Napi::Value PsiServerWrap::CreateWithNewKey(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  const bool reveal_intersection = info[0].As<Napi::Boolean>().Value();
  return Wrap(env, PsiServer::CreateWithNewKey(reveal_intersection));
}

Napi::Value PsiServerWrap::CreateFromKey(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  const std::string key = ToByteString(info[0]);
  const bool reveal_intersection = info[1].As<Napi::Boolean>().Value();
  return Wrap(env, PsiServer::CreateFromKey(key, reveal_intersection));
}

Napi::Value PsiServerWrap::CreateSetupMessage(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (ThrowIfDeleted(env, server_.get(), "PsiServer")) return env.Undefined();
  const double fpr = info[0].As<Napi::Number>().DoubleValue();
  const int64_t num_client_inputs = info[1].As<Napi::Number>().Int64Value();
  const std::vector<std::string> inputs =
      ToStringVector(info[2].As<Napi::Array>());
  const DataStructure ds =
      static_cast<DataStructure>(info[3].As<Napi::Number>().Int32Value());
  const bool include_permutation =
      info.Length() > 4 && info[4].As<Napi::Boolean>().Value();
  int32_t* progress = ProgressSlot(info, 5);

  // The Raw container reorders (sorts) its elements and reports the permutation
  // that undoes that reordering. Raw::Create expects a permutation vector
  // pre-sized to the input count, so allocate one whenever Raw is requested --
  // exactly as the emscripten binding does.
  std::unique_ptr<std::vector<std::size_t>> permutation;
  if (ds == DataStructure::Raw) {
    permutation = std::make_unique<std::vector<std::size_t>>(inputs.size());
  }

  absl::StatusOr<psi_proto::ServerSetup> setup = server_->CreateSetupMessage(
      fpr, num_client_inputs, inputs, ds, permutation.get(), progress);
  if (!setup.ok()) {
    return MakeError(env, setup.status());
  }

  const size_t size = setup->ByteSizeLong();
  Napi::Uint8Array bytes = Napi::Uint8Array::New(env, size);
  setup->SerializeToArray(bytes.Data(), static_cast<int>(size));

  Napi::Object result = Napi::Object::New(env);
  result.Set("Value", bytes);
  result.Set("Status", env.Null());
  if (include_permutation && ds == DataStructure::Raw) {
    result.Set("Permutation", ToNumberArray(env, *permutation));
  }
  return result;
}

Napi::Value PsiServerWrap::ProcessRequest(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (ThrowIfDeleted(env, server_.get(), "PsiServer")) return env.Undefined();
  const std::string request_bytes = ToByteString(info[0]);
  int32_t* progress = ProgressSlot(info, 1);
  psi_proto::Request request;
  if (!request.ParseFromArray(request_bytes.data(),
                              static_cast<int>(request_bytes.size()))) {
    return MakeError(env,
                     absl::InvalidArgumentError("failed to parse client request"));
  }
  absl::StatusOr<psi_proto::Response> response =
      server_->ProcessRequest(request, progress);
  if (!response.ok()) {
    return MakeError(env, response.status());
  }
  return SerializedOk(env, *response);
}

Napi::Value PsiServerWrap::GetPrivateKeyBytes(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (ThrowIfDeleted(env, server_.get(), "PsiServer")) return env.Undefined();
  return ToUint8Array(env, server_->GetPrivateKeyBytes());
}

Napi::Value PsiServerWrap::Delete(const Napi::CallbackInfo& info) {
  server_.reset();
  return info.Env().Undefined();
}

// ---------------------------------------------------------------------------
// PsiClientWrap
// ---------------------------------------------------------------------------

class PsiClientWrap : public Napi::ObjectWrap<PsiClientWrap> {
 public:
  static Napi::Function DefineConstructor(Napi::Env env);
  static Napi::Object MakeFactory(Napi::Env env);

  explicit PsiClientWrap(const Napi::CallbackInfo& info);

 private:
  static Napi::Value CreateWithNewKey(const Napi::CallbackInfo& info);
  static Napi::Value CreateFromKey(const Napi::CallbackInfo& info);
  static Napi::Value Wrap(
      Napi::Env env, absl::StatusOr<std::unique_ptr<PsiClient>> client);

  Napi::Value CreateRequest(const Napi::CallbackInfo& info);
  Napi::Value GetIntersection(const Napi::CallbackInfo& info);
  Napi::Value GetAssociationTable(const Napi::CallbackInfo& info);
  Napi::Value GetIntersectionSize(const Napi::CallbackInfo& info);
  Napi::Value GetPrivateKeyBytes(const Napi::CallbackInfo& info);
  Napi::Value Delete(const Napi::CallbackInfo& info);

  // Parses the (server_setup, server_response) pair the intersection methods
  // share. On a parse failure it fills `*error` with a { Value, Status } error
  // object and returns false.
  bool ParseSetupAndResponse(const Napi::CallbackInfo& info,
                             psi_proto::ServerSetup* setup,
                             psi_proto::Response* response, Napi::Value* error);

  std::unique_ptr<PsiClient> client_;
};

Napi::Function PsiClientWrap::DefineConstructor(Napi::Env env) {
  return DefineClass(
      env, "PsiClient",
      {
          InstanceMethod("CreateRequest", &PsiClientWrap::CreateRequest),
          InstanceMethod("GetIntersection", &PsiClientWrap::GetIntersection),
          InstanceMethod("GetAssociationTable",
                         &PsiClientWrap::GetAssociationTable),
          InstanceMethod("GetIntersectionSize",
                         &PsiClientWrap::GetIntersectionSize),
          InstanceMethod("GetPrivateKeyBytes",
                         &PsiClientWrap::GetPrivateKeyBytes),
          InstanceMethod("delete", &PsiClientWrap::Delete),
      });
}

Napi::Object PsiClientWrap::MakeFactory(Napi::Env env) {
  Napi::Object factory = Napi::Object::New(env);
  factory.Set("CreateWithNewKey",
              Napi::Function::New(env, &PsiClientWrap::CreateWithNewKey));
  factory.Set("CreateFromKey",
              Napi::Function::New(env, &PsiClientWrap::CreateFromKey));
  return factory;
}

PsiClientWrap::PsiClientWrap(const Napi::CallbackInfo& info)
    : Napi::ObjectWrap<PsiClientWrap>(info) {
  // See PsiServerWrap: move the instance out of the External's heap unique_ptr.
  client_ = std::move(
      *info[0].As<Napi::External<std::unique_ptr<PsiClient>>>().Data());
}

Napi::Value PsiClientWrap::Wrap(
    Napi::Env env, absl::StatusOr<std::unique_ptr<PsiClient>> client) {
  if (!client.ok()) {
    return MakeError(env, client.status());
  }
  AddonData* data = env.GetInstanceData<AddonData>();
  // Owned by a heap unique_ptr carried by the External (finalizer frees it), so
  // the instance is reclaimed even if constructor.New() throws before the
  // wrapper takes ownership. See PsiServerWrap::Wrap.
  auto* owned = new std::unique_ptr<PsiClient>(std::move(*client));
  Napi::External<std::unique_ptr<PsiClient>> external =
      Napi::External<std::unique_ptr<PsiClient>>::New(
          env, owned,
          [](Napi::Env, std::unique_ptr<PsiClient>* held) { delete held; });
  Napi::Object instance = data->client_ctor.New({external});
  return MakeOk(env, instance);
}

Napi::Value PsiClientWrap::CreateWithNewKey(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  const bool reveal_intersection = info[0].As<Napi::Boolean>().Value();
  return Wrap(env, PsiClient::CreateWithNewKey(reveal_intersection));
}

Napi::Value PsiClientWrap::CreateFromKey(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  const std::string key = ToByteString(info[0]);
  const bool reveal_intersection = info[1].As<Napi::Boolean>().Value();
  return Wrap(env, PsiClient::CreateFromKey(key, reveal_intersection));
}

Napi::Value PsiClientWrap::CreateRequest(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (ThrowIfDeleted(env, client_.get(), "PsiClient")) return env.Undefined();
  const std::vector<std::string> inputs =
      ToStringVector(info[0].As<Napi::Array>());
  int32_t* progress = ProgressSlot(info, 1);
  absl::StatusOr<psi_proto::Request> request =
      client_->CreateRequest(inputs, progress);
  if (!request.ok()) {
    return MakeError(env, request.status());
  }
  return SerializedOk(env, *request);
}

bool PsiClientWrap::ParseSetupAndResponse(const Napi::CallbackInfo& info,
                                          psi_proto::ServerSetup* setup,
                                          psi_proto::Response* response,
                                          Napi::Value* error) {
  Napi::Env env = info.Env();
  const std::string setup_bytes = ToByteString(info[0]);
  if (!setup->ParseFromArray(setup_bytes.data(),
                             static_cast<int>(setup_bytes.size()))) {
    *error = MakeError(
        env, absl::InvalidArgumentError("failed to parse server setup"));
    return false;
  }
  const std::string response_bytes = ToByteString(info[1]);
  if (!response->ParseFromArray(response_bytes.data(),
                                static_cast<int>(response_bytes.size()))) {
    *error = MakeError(
        env, absl::InvalidArgumentError("failed to parse server response"));
    return false;
  }
  return true;
}

Napi::Value PsiClientWrap::GetIntersection(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (ThrowIfDeleted(env, client_.get(), "PsiClient")) return env.Undefined();
  psi_proto::ServerSetup setup;
  psi_proto::Response response;
  Napi::Value error;
  if (!ParseSetupAndResponse(info, &setup, &response, &error)) {
    return error;
  }
  absl::StatusOr<std::vector<int64_t>> intersection =
      client_->GetIntersection(setup, response, ProgressSlot(info, 2));
  if (!intersection.ok()) {
    return MakeError(env, intersection.status());
  }
  return MakeOk(env, ToNumberArray(env, *intersection));
}

Napi::Value PsiClientWrap::GetAssociationTable(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (ThrowIfDeleted(env, client_.get(), "PsiClient")) return env.Undefined();
  psi_proto::ServerSetup setup;
  psi_proto::Response response;
  Napi::Value error;
  if (!ParseSetupAndResponse(info, &setup, &response, &error)) {
    return error;
  }
  absl::StatusOr<std::pair<std::vector<std::size_t>, std::vector<std::size_t>>>
      table = client_->GetAssociationTable(setup, response,
                                           ProgressSlot(info, 2));
  if (!table.ok()) {
    return MakeError(env, table.status());
  }
  Napi::Array pair = Napi::Array::New(env, 2);
  pair.Set(0u, ToNumberArray(env, table->first));
  pair.Set(1u, ToNumberArray(env, table->second));
  return MakeOk(env, pair);
}

Napi::Value PsiClientWrap::GetIntersectionSize(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (ThrowIfDeleted(env, client_.get(), "PsiClient")) return env.Undefined();
  psi_proto::ServerSetup setup;
  psi_proto::Response response;
  Napi::Value error;
  if (!ParseSetupAndResponse(info, &setup, &response, &error)) {
    return error;
  }
  absl::StatusOr<int64_t> size =
      client_->GetIntersectionSize(setup, response, ProgressSlot(info, 2));
  if (!size.ok()) {
    return MakeError(env, size.status());
  }
  return MakeOk(env, Napi::Number::New(env, static_cast<double>(*size)));
}

Napi::Value PsiClientWrap::GetPrivateKeyBytes(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (ThrowIfDeleted(env, client_.get(), "PsiClient")) return env.Undefined();
  return ToUint8Array(env, client_->GetPrivateKeyBytes());
}

Napi::Value PsiClientWrap::Delete(const Napi::CallbackInfo& info) {
  client_.reset();
  return info.Env().Undefined();
}

// ---------------------------------------------------------------------------
// Module init
// ---------------------------------------------------------------------------

Napi::Object Init(Napi::Env env, Napi::Object exports) {
  auto data = std::make_unique<AddonData>();
  data->server_ctor = Napi::Persistent(PsiServerWrap::DefineConstructor(env));
  data->client_ctor = Napi::Persistent(PsiClientWrap::DefineConstructor(env));
  env.SetInstanceData<AddonData>(data.release());

  exports.Set("PsiServer", PsiServerWrap::MakeFactory(env));
  exports.Set("PsiClient", PsiClientWrap::MakeFactory(env));

  Napi::Object data_structure = Napi::Object::New(env);
  data_structure.Set("Raw",
                     Napi::Number::New(env, static_cast<int>(DataStructure::Raw)));
  data_structure.Set("GCS",
                     Napi::Number::New(env, static_cast<int>(DataStructure::Gcs)));
  data_structure.Set(
      "BloomFilter",
      Napi::Number::New(env, static_cast<int>(DataStructure::BloomFilter)));
  exports.Set("DataStructure", data_structure);

  Napi::Object package = Napi::Object::New(env);
  package.Set("version",
              Napi::Function::New(env, [](const Napi::CallbackInfo& info) {
                return Napi::String::New(info.Env(),
                                         std::string(Package::kVersion));
              }));
  exports.Set("Package", package);

  return exports;
}

}  // namespace

NODE_API_MODULE(psi_native, Init)
