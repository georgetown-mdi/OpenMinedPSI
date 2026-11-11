#!/usr/bin/env bash
#
# Builds the platform-independent half of the publishable @openmined/psi.js dist/
# tree: the WASM bundles (node, web, worker) and the rolled-up JS plus type
# declarations and package metadata. It leaves
# private_set_intersection/javascript/dist/ ready to pack but does NOT fold in
# the native prebuilds or run `npm pack` -- that is pack-native-package.sh's job.
#
# The protobuf bindings (bin/psi_pb.js, src/implementation/proto/psi_pb.d.ts)
# are the committed copies; build-proto.sh regenerates them, and the Protobuf
# bindings workflow checks that the committed copies match its output.
#
# Splitting the two phases lets this arch-independent work (WASM/rollup,
# ~7 min) run in a job that starts immediately and in parallel with the native
# addon matrix, instead of serializing behind the slow Windows addon build.
#
# Run from the repo root. Requires `npm ci` to have run first (rollup and its
# plugins live in node_modules).

set -euo pipefail

R="private_set_intersection/javascript"

echo "== building WASM variants (arch-independent) =="
bazel build --config=wasm -c opt \
  "//$R/cpp:psi_wasm_node.js" \
  "//$R/cpp:psi_wasm_web.js" \
  "//$R/cpp:psi_wasm_worker.js"

echo "== copying WASM bundles to bin/ (committed psi_pb.js used as is) =="
cp -f "bazel-bin/$R/cpp/psi_wasm_node.js/wasm_node.js" "$R/bin/psi_wasm_node.js"
cp -f "bazel-bin/$R/cpp/psi_wasm_web.js/wasm_web.js" "$R/bin/psi_wasm_web.js"
cp -f "bazel-bin/$R/cpp/psi_wasm_worker.js/wasm_worker.js" "$R/bin/psi_wasm_worker.js"

echo "== rollup all bundles (incl. native) =="
rm -rf "$R/dist"
mkdir -p "$R/dist"
NODE_OPTIONS=--max_old_space_size=16384 npx rollup -c

# The native bundle is emitted as psi_native_node.js but rollup-plugin-typescript2
# names its declaration after the input (native_node.d.ts). Emit a matching
# psi_native_node.d.ts so a `@openmined/psi.js/psi_native_node.js` subpath import
# resolves types.
printf 'export { default } from "./native_node";\n' > "$R/dist/psi_native_node.d.ts"

echo "== copying package metadata (mirrors postrollup.sh) =="
cp -f package.json "$R/dist/"
cp -f "$R/README.md" "$R/dist/"
cp -f LICENSE "$R/dist/"
cp -f CHANGES.md "$R/dist/"
mkdir -p "$R/dist/implementation/proto"
cp -f "$R/src/implementation/proto/psi_pb.d.ts" "$R/dist/implementation/proto/"

echo "== bundle ready: $R/dist (prebuilds folded in + packed by pack-native-package.sh) =="
