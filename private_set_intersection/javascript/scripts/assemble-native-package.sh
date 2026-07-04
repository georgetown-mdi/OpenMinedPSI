#!/usr/bin/env bash
#
# Assembles the publishable @openmined/psi.js dist/ -- the WASM bundles (node,
# web, worker), the native addon entry, and the per-platform native prebuilds --
# and packs the tarball.
#
# The native .node binaries are platform-specific and built by separate jobs
# (one per OS/arch); pass a directory containing their `<platform>-<arch>/
# node.napi.node` layout as $1 and they are folded into the tarball's
# prebuilds/. With no argument the tarball carries only the WASM engine (every
# platform then falls back to WASM). Run from the repo root.
#
# Usage: scripts/assemble-native-package.sh [PREBUILDS_DIR]

set -euo pipefail

R="private_set_intersection/javascript"
PREBUILDS_SRC="${1:-}"

echo "== building WASM variants (arch-independent) =="
bazel build --config=wasm -c opt \
  "//$R/cpp:psi_wasm_node.js" \
  "//$R/cpp:psi_wasm_web.js" \
  "//$R/cpp:psi_wasm_worker.js"

echo "== refreshing bin/ (checked-in psi_pb.js kept) =="
cp -f "bazel-bin/$R/cpp/psi_wasm_node.js/wasm_node.js" "$R/bin/psi_wasm_node.js"
cp -f "bazel-bin/$R/cpp/psi_wasm_web.js/wasm_web.js" "$R/bin/psi_wasm_web.js"
cp -f "bazel-bin/$R/cpp/psi_wasm_worker.js/wasm_worker.js" "$R/bin/psi_wasm_worker.js"

# Generate the protobuf bindings (psi_pb.js -> bin/, psi_pb.d.ts -> src/). They
# are gitignored, so a clean checkout lacks them and rollup cannot resolve
# ./proto/psi_pb. This script reimplements prerollup.sh but had skipped the step;
# build-proto.sh needs the host protoc built first.
echo "== generating protobuf bindings (psi_pb) =="
bazel build -c opt --platforms="@local_config_platform//:host" @protobuf//:protoc
bash "$R/scripts/build-proto.sh"

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

if [ -n "$PREBUILDS_SRC" ] && [ -d "$PREBUILDS_SRC" ]; then
  echo "== staging native prebuilds from $PREBUILDS_SRC =="
  mkdir -p "$R/dist/prebuilds"
  cp -a "$PREBUILDS_SRC"/. "$R/dist/prebuilds/"
  find "$R/dist/prebuilds" -name '*.node' -print
else
  echo "== no prebuilds supplied: tarball is WASM-only =="
fi

echo "== pack =="
( cd "$R/dist" && npm pack )
ls -la "$R/dist"/openmined-psi.js-*.tgz
