#
# Copyright 2020 the authors listed in CONTRIBUTORS.md
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
load("@rules_cc//cc:cc_binary.bzl", "cc_binary")

package(default_visibility = ["//visibility:public"])

# The native Node-API addon. It reuses the same C++ PsiServer / PsiClient /
# BoringSSL dep graph as the emscripten (WASM) binding, so the two backends emit
# identical wire bytes by construction.
#
# The Node process provides the N-API symbols at load time, so the addon is
# linked leaving them undefined:
#   - Linux: `ld -shared` allows undefined symbols by default.
#   - macOS: `-undefined dynamic_lookup` defers them to the loader.
#   - Windows: they are satisfied by linking against the Node import library,
#     which the CI prebuild job supplies (see the prebuild workflow); it is not
#     wired here because this repo has no vendored node.lib.
#
# `linkshared` makes cc_binary emit a shared library, but under Bazel's
# `lib<name>.so` naming; the `addon` genrule below copies it to a plain
# `psi_native.node` that Node can dlopen and node-gyp-build can resolve.
cc_binary(
    name = "psi_native_shared",
    srcs = ["psi_napi.cpp"],
    # Only the Linux/ELF link consumes the version script (see linkopts below);
    # keep it off the macOS/Windows link actions entirely.
    additional_linker_inputs = select({
        "@platforms//os:osx": [],
        "@platforms//os:windows": [],
        "//conditions:default": ["napi.lds"],
    }),
    linkshared = True,
    linkopts = select({
        "@platforms//os:osx": ["-Wl,-undefined,dynamic_lookup"],
        "@platforms//os:windows": [],
        # Linux/ELF. -pthread: parallel_ec uses std::thread; older glibc /
        # non-glibc libc need it explicitly (recent glibc folds it into libc, a
        # harmless no-op there). The version script exports ONLY the Node entry
        # point and binds every other defined symbol locally (local: *), so the
        # symbols pulled from static archives (BoringSSL, protobuf, abseil) and
        # the addon's statically linked libc++ (zig cc) stay out of the dynamic
        # table and are never interposed by the host process's libstdc++ -- see
        # napi.lds. (zig cc's linker driver rejects --exclude-libs, but the
        # version script's local: * already achieves the same hiding.)
        "//conditions:default": [
            "-pthread",
            "-Wl,--version-script,$(location napi.lds)",
        ],
    }),
    deps = [
        "//private_set_intersection/cpp:package",
        "//private_set_intersection/cpp:psi_client",
        "//private_set_intersection/cpp:psi_server",
        "//private_set_intersection/cpp/datastructure",
        "//private_set_intersection/proto:psi_cc_proto",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@node_addon_api",
    ],
)

genrule(
    name = "addon",
    srcs = [":psi_native_shared"],
    outs = ["psi_native.node"],
    cmd = "cp $(location :psi_native_shared) $@",
)
