import loadNativeLibrary from './native_raw'

import { Loader, createLoader } from './main/loader'
import { PSILibrary } from './implementation/psi'
import { PSI } from './main/psi'

const Loader = (): Promise<Loader> => createLoader(loadNativeLibrary)

/**
 * Native (Node-only) entry point. Loads the prebuilt N-API addon and wraps it
 * with the SAME implementation/ layer as the WASM builds, so the returned
 * PSILibrary is interface- and wire-compatible with `@openmined/psi.js` on
 * WASM. Resolves rejected when no prebuild exists for the platform (see
 * native_raw); the caller falls back to WASM.
 */
export default async (): Promise<PSILibrary> => {
  const psi = await PSI(Loader)
  return psi({ client: true, server: true })
}
