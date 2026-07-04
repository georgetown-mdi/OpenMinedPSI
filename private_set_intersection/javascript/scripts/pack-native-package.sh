#!/usr/bin/env bash
#
# Folds the per-platform native prebuilds into an already-built dist/ tree (see
# build-bundle.sh) and packs the @openmined/psi.js tarball. This is the cheap
# tail of the pipeline: it runs no compiler, so it can wait on both the bundle
# and the addon matrix and finish in seconds.
#
# Pass a directory containing the `<platform>-<arch>/node.napi.node` prebuild
# layout as $1 and the .node binaries are folded into the tarball's prebuilds/.
# With no argument the tarball carries only the WASM engine (every platform then
# falls back to WASM). Assumes private_set_intersection/javascript/dist/ already
# exists. Run from the repo root.
#
# Usage: scripts/pack-native-package.sh [PREBUILDS_DIR]

set -euo pipefail

R="private_set_intersection/javascript"
PREBUILDS_SRC="${1:-}"

if [ ! -d "$R/dist" ]; then
  echo "::error::$R/dist not found -- run build-bundle.sh (or download the bundle artifact) first" >&2
  exit 1
fi

# Drop the committed dist/.gitignore placeholder if a `git checkout` restored it
# (the package job checks out the repo for this script). build-bundle.sh wipes
# dist/ from scratch, so it never carries the placeholder; npm otherwise packs it
# into the tarball (the `!.gitignore` negation + files:["**/*"] pull it in).
rm -f "$R/dist/.gitignore"

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
