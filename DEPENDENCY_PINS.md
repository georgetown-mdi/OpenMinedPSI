# Pinned build inputs and their update routes

Every toolchain, module, action and downloaded file this fork's builds pin,
with where each is pinned and the route that offers it for upgrade. The
`Native prebuilds` workflow builds the `@openmined/psi.js` tarball that
georgetown-mdi/alcove vendors, so a row on that path changes what ships.

- **automatic**: a block in `.github/dependabot.yml` reaches the pin and opens
  a pull request.
- **manual**: nothing offers an upgrade; the pin moves when someone checks
  upstream and edits it, by the check the row names.

A route says how an upgrade is offered, not how it is reviewed. The weekly
scheduled runs of `CI` and `Native prebuilds` show when a floating input has
broken a build, not when a pin has fallen behind.

| Pin | Where | Recorded as | Route | How it moves |
| --- | --- | --- | --- | --- |
| npm packages | `package.json`, `package-lock.json` | Ranges in the manifest, versions and `integrity` in the lockfile | automatic | The `npm` block, weekly, with a cooldown. The two runtime dependencies (`google-protobuf`, `node-gyp-build`) arrive one pull request each; the development tree's minor and patch bumps arrive grouped. Rollup, its plugins, TypeScript and the protoc plugins build the shipped bundle. |
| Bazel | `.bazelversion` (CI, through bazelisk); `Dockerfile` `BAZEL_VERSION` and `BAZEL_DIST_SHA256` (dev container) | The exact version `8.2.1`; the dist zip's sha256 | manual | Compare with the latest Bazel release. Move `.bazelversion` and the `Dockerfile` together, taking the sha256 from the release's `bazel-<version>-dist.zip.sha256`. |
| bazelisk | `bazelisk-version` on the `JS` job in `CI.yml`; `@bazel/bazelisk` in `package.json` | `1.26` on that job, setup-bazel's default on the others; an npm range | manual for the workflow input, automatic for the npm range | Compare with the latest bazelisk release when setup-bazel moves. |
| Bazel modules | `MODULE.bazel` `bazel_dep` lines, the `single_version_override` entries for the patched `boringssl` and `rules_swift`, and `MODULE.bazel.lock` | Exact versions; the lockfile's registry file hashes | manual | Compare each `bazel_dep` with the Bazel Central Registry and its upstream (`OpenMined/PSI` pins the same versions). A patched override moves with its patch under `patches/`. `hermetic_cc_toolchain` sets the zig SDK and so the shipped Linux addons' libc floor. |
| Archives fetched by `http_archive` | `MODULE.bazel`: `private_join_and_compute`, `node_api_headers` 1.9.0, `node_addon_api` 8.9.0 | A commit or exact version in the URL, and a `sha256` | manual | Compare with the source repository or the npm registry; move the URL and `sha256` together. |
| emsdk | `MODULE.bazel` `git_override` and `emsdk_version` | The commit `62a853cd` that tag `4.0.10` names, and the version `4.0.10` | manual | Compare with the latest emsdk tag; resolve the new tag with `git ls-remote https://github.com/emscripten-core/emsdk.git refs/tags/<version>` and move the commit and `emsdk_version` together. The emscripten binaries it downloads are checksummed in `MODULE.bazel.lock`. |
| Toolchains Bazel downloads | `MODULE.bazel`: `rust.toolchain` 1.87.0, `go_sdk.download` 1.24.3, `python.toolchain` 3.9 to 3.13, the `crate.spec` versions; `private_set_intersection/python/requirements/` lock files | Exact versions | manual | Compare with each upstream. None enters the JavaScript tarball. |
| Node import library (`node.lib`) | `native-prebuilds.yml`, Windows leg, `NODE_LIB_VERSION` and `NODE_LIB_SHA256` | The Node release `22.23.3` and the sha256 of its `win-x64/node.lib` | manual | Move both together, taking the sha256 from the new release's `SHASUMS256.txt`. It is the only file of the Node release the addon link uses, fetched directly rather than through node-gyp. |
| Node runtime in workflows | `actions/setup-node` `node-version` in `native-prebuilds.yml`; the `validate-linux` container images | The major `22`; the tags `node:22-bookworm-slim` and `node:22-alpine` | manual | Float within the major on purpose: they run the build tools and the load tests, and the shipped addon's ABI is fixed by the N-API level, not by them. Move the major by hand. |
| Rust toolchain in `CI` | `dtolnay/rust-toolchain` in the `Rust` job of `CI.yml` | A commit of the action's `master` branch, with `toolchain: stable` | manual | The action publishes no releases; compare with its `master` branch. The host toolchain floats with `stable`; the Rust build itself uses the `rust.toolchain` version above. Whether Dependabot updates a commit pin with no release is unmeasured. |
| Swift toolchain (Windows) | `compnerd/gha-setup-swift` inputs in `native-prebuilds.yml` | The release names `swift-6.3.3-release` and `6.3.3-RELEASE`; the installer is a download with no checksum | manual | Compare with the latest Swift release and move both inputs together. The action downloads the Windows installer from download.swift.org with no checksum (its `cache` input is off here, so no run restores an installer another job saved), runs it elevated, and then rewrites `GITHUB_PATH` and `GITHUB_ENV` from the machine and user environment, which the `win32-x64` `Build addon` step inherits. The addon compiles no Swift; the install satisfies the `rules_swift` toolchain probe. |
| Go lint tool | `go install golang.org/x/lint/golint@latest` in `CI.yml` | Not pinned | manual | Unpinned; CI only. |
| Dev container base | `Dockerfile` `FROM ubuntu:26.04` | A tag | manual | Not on any build path; the image is for local development. |
| `actions/*` actions | Every workflow | A floating major tag (`actions/checkout@v7`) | automatic | The `github-actions` block offers major bumps; minor and patch bumps are ignored because the tag already tracks them. |
| Third-party actions | `bazel-contrib/setup-bazel`, `dorny/paths-filter`, `compnerd/gha-setup-swift` | A commit with the release tag in a trailing comment | automatic | The `github-actions` block, weekly, with a cooldown. |
