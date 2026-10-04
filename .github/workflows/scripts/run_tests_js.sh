#!/bin/sh
set -e

# JavaScript
npm install

npm run build
npm run build:proto
npm run compile

# The native addon, so the suites that cover both bindings run both.
bazel build -c opt --config=napi //private_set_intersection/napi:addon
PSI_REQUIRE_NATIVE=1 npm run test
