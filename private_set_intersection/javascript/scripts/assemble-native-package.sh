#!/usr/bin/env bash
#
# Assembles the publishable @openmined/psi.js dist/ -- the WASM bundles (node,
# web, worker), the native addon entry, and the per-platform native prebuilds --
# and packs the tarball in one shot.
#
# This is a thin wrapper over the two phases CI runs as separate parallel jobs:
#   1. build-bundle.sh          -- the arch-independent WASM/protoc/rollup work
#   2. pack-native-package.sh   -- fold in the native prebuilds and `npm pack`
# Keeping the wrapper preserves the single-command local / one-shot build path.
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

bash "$R/scripts/build-bundle.sh"
bash "$R/scripts/pack-native-package.sh" "${1:-}"
