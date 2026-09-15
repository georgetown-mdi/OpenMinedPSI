load("@emsdk//emscripten_toolchain:wasm_rules.bzl", "wasm_cc_binary")
load("@rules_cc//cc:cc_binary.bzl", "cc_binary")

package(default_visibility = ["//visibility:public"])

DEFAULT_EMSCRIPTEN_LINKOPTS = [
    "--bind",  # Compiles the source code using the Embind bindings to connect C/C++ and JavaScript
    "-flto",
    "--closure 1",  # Run the closure compiler
    "-s MALLOC=emmalloc",  # Switch to using the much smaller implementation
    "-s ALLOW_MEMORY_GROWTH=1",  # Allow both asm.js and wasm to increase memory dynamically
    "-s USE_PTHREADS=0",  # Ensure we disable pthreads
    "-s ASSERTIONS=0",  # Turn off runtime assertions
    "-s SINGLE_FILE=1",  # Pack all webassembly into base64
    "-s DISABLE_EXCEPTION_CATCHING=1",  # Disable all exception catching
    "-s NODEJS_CATCH_EXIT=0",  # We don't have a 'main' so disable exit() catching
    "-s WASM=1",  # Specify wasm ouptut
    "-s ERROR_ON_UNDEFINED_SYMBOLS=0",  # Ignore POSIX popen, __Unwind_GetIP
    "-s MODULARIZE=1",  # Allows us to manually invoke the initialization of wasm
]

NODE_LINKOPTS = [
    "-s EXPORT_ES6=0",
    "-s ENVIRONMENT=node",
]

WEB_LINKOPTS = [
    "-s EXPORT_ES6=1",  # Export as es6 module, used for rollup
    "-s ENVIRONMENT=web,webview",
]

WORKER_LINKOPTS = [
    "-s EXPORT_ES6=1",  # Export as es6 module, used for rollup
    "-s ENVIRONMENT=worker",
]

# Evaluation-only pthreads variant (not shipped, not in build-psi-wasm.sh).
# Build with --config=wasm --config=wasm_threads so every translation unit is
# compiled with -pthread -DPSI_ENABLE_THREADS; add --config=wasm_simd_threads
# instead for the SIMD+threads variant. The pool must hold at least
# hardware_concurrency - 1 workers: parallel_ec.cpp spawns up to that many from
# a synchronous call, which cannot wait for a new worker to load.
THREADS_EMSCRIPTEN_LINKOPTS = [
    opt
    for opt in DEFAULT_EMSCRIPTEN_LINKOPTS
    if opt != "-s USE_PTHREADS=0"
] + [
    "-pthread",
    "-s PTHREAD_POOL_SIZE=navigator.hardwareConcurrency",
    "-s PROXY_TO_PTHREAD=0",
]

# emcc requires a threaded build's ENVIRONMENT to include worker, where the
# pthreads run.
THREADS_WEB_LINKOPTS = [
    "-s EXPORT_ES6=1",
    "-s ENVIRONMENT=web,webview,worker",
]

# psi server + client build
cc_binary(
    name = "wasm_node.js",
    srcs = [
        "client.cpp",
        "data_structure.cpp",
        "package.cpp",
        "server.cpp",
        "utils.h",
    ],
    linkopts = DEFAULT_EMSCRIPTEN_LINKOPTS + NODE_LINKOPTS,
    visibility = ["//visibility:private"],
    deps = [
        "//private_set_intersection/cpp:package",
        "//private_set_intersection/cpp:psi_client",
        "//private_set_intersection/cpp:psi_server",
    ],
)

cc_binary(
    name = "wasm_web.js",
    srcs = [
        "client.cpp",
        "data_structure.cpp",
        "package.cpp",
        "server.cpp",
        "utils.h",
    ],
    linkopts = DEFAULT_EMSCRIPTEN_LINKOPTS + WEB_LINKOPTS,
    visibility = ["//visibility:private"],
    deps = [
        "//private_set_intersection/cpp:package",
        "//private_set_intersection/cpp:psi_client",
        "//private_set_intersection/cpp:psi_server",
    ],
)

cc_binary(
    name = "wasm_worker.js",
    srcs = [
        "client.cpp",
        "data_structure.cpp",
        "package.cpp",
        "server.cpp",
        "utils.h",
    ],
    linkopts = DEFAULT_EMSCRIPTEN_LINKOPTS + WORKER_LINKOPTS,
    visibility = ["//visibility:private"],
    deps = [
        "//private_set_intersection/cpp:package",
        "//private_set_intersection/cpp:psi_client",
        "//private_set_intersection/cpp:psi_server",
    ],
)

wasm_cc_binary(
    name = "psi_wasm_node.js",
    cc_target = ":wasm_node.js",
)

wasm_cc_binary(
    name = "psi_wasm_web.js",
    cc_target = ":wasm_web.js",
)

wasm_cc_binary(
    name = "psi_wasm_worker.js",
    cc_target = ":wasm_worker.js",
)

cc_binary(
    name = "wasm_node_threads.js",
    srcs = [
        "client.cpp",
        "data_structure.cpp",
        "package.cpp",
        "server.cpp",
        "utils.h",
    ],
    linkopts = THREADS_EMSCRIPTEN_LINKOPTS + NODE_LINKOPTS,
    visibility = ["//visibility:private"],
    deps = [
        "//private_set_intersection/cpp:package",
        "//private_set_intersection/cpp:psi_client",
        "//private_set_intersection/cpp:psi_server",
    ],
)

wasm_cc_binary(
    name = "psi_wasm_node_threads.js",
    cc_target = ":wasm_node_threads.js",
)

cc_binary(
    name = "wasm_web_threads.js",
    srcs = [
        "client.cpp",
        "data_structure.cpp",
        "package.cpp",
        "server.cpp",
        "utils.h",
    ],
    linkopts = THREADS_EMSCRIPTEN_LINKOPTS + THREADS_WEB_LINKOPTS,
    visibility = ["//visibility:private"],
    deps = [
        "//private_set_intersection/cpp:package",
        "//private_set_intersection/cpp:psi_client",
        "//private_set_intersection/cpp:psi_server",
    ],
)

wasm_cc_binary(
    name = "psi_wasm_web_threads.js",
    cc_target = ":wasm_web_threads.js",
)

cc_binary(
    name = "wasm_worker_threads.js",
    srcs = [
        "client.cpp",
        "data_structure.cpp",
        "package.cpp",
        "server.cpp",
        "utils.h",
    ],
    linkopts = THREADS_EMSCRIPTEN_LINKOPTS + WORKER_LINKOPTS,
    visibility = ["//visibility:private"],
    deps = [
        "//private_set_intersection/cpp:package",
        "//private_set_intersection/cpp:psi_client",
        "//private_set_intersection/cpp:psi_server",
    ],
)

wasm_cc_binary(
    name = "psi_wasm_worker_threads.js",
    cc_target = ":wasm_worker_threads.js",
)

# Benchmark
cc_binary(
    name = "benchmark_wasm.js",
    srcs = ["//private_set_intersection/cpp:psi_benchmark.cpp"],
    linkopts = DEFAULT_EMSCRIPTEN_LINKOPTS + NODE_LINKOPTS,
    visibility = ["//visibility:private"],
    deps = [
        "//private_set_intersection/cpp:psi_client",
        "//private_set_intersection/cpp:psi_server",
        "@google_benchmark//:benchmark_main",
    ],
)

wasm_cc_binary(
    name = "psi_benchmark_wasm.js",
    cc_target = ":benchmark_wasm.js",
)
