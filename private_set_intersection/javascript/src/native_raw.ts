import nodeGypBuild from 'node-gyp-build'
import type * as psi from 'psi_'

// __dirname resolves, in the bundled CommonJS output, to the package root where
// the prebuilt addons live under prebuilds/<platform>-<arch>/. It is provided
// by Node at runtime for the CJS bundle; declared here so the ESM source
// type-checks.
declare const __dirname: string

/**
 * Loads the native N-API addon for the running platform via node-gyp-build,
 * which resolves prebuilds/<platform>-<arch>/*.node. The addon exposes the same
 * raw-module surface as the emscripten Module (PsiServer / PsiClient /
 * DataStructure / Package returning the { Value, Status } shape), so it IS a
 * psi.Library and flows through the shared implementation/ wrapper layer
 * unchanged. Throws when no prebuild exists for this platform; callers treat
 * that as "native unavailable" and fall back to WASM.
 */
const loadNativeLibrary = (): psi.Library =>
  nodeGypBuild(__dirname) as unknown as psi.Library

// Matches the emscripten module factory shape `() => Promise<psi.Library>`
// consumed by createLoader; the native addon needs no async instantiation.
export default (): Promise<psi.Library> =>
  Promise.resolve(loadNativeLibrary())
