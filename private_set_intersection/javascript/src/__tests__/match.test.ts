import { createHash } from 'crypto'
import * as fs from 'fs'
import * as path from 'path'
import type * as psi from 'psi_'
import { ERROR_INSTANCE_DELETED } from '../implementation/constants'
import { Client } from '../implementation/client'
import { MatchResult } from '../implementation/match'
import { ServerSetup } from '../implementation/proto/psi_pb'
import { PSILibrary } from '../implementation/psi'
import { createLoader } from '../main/loader'
import { PSI } from '../main/psi'
import PSIWasm from '../wasm_node'

// The native addon the suite also runs against: PSI_NATIVE_ADDON, else the
// Bazel output of //private_set_intersection/napi:addon. Without one the native
// half is skipped, unless PSI_REQUIRE_NATIVE is set, when it fails.
const nativeAddonPath =
  process.env.PSI_NATIVE_ADDON ??
  path.resolve(
    __dirname,
    '../../../../bazel-bin/private_set_intersection/napi/psi_native.node'
  )
const haveNative = fs.existsSync(nativeAddonPath)

// Loaded with process.dlopen, past Jest's module mapping of psi_* names.
const loadNative = async (): Promise<PSILibrary> => {
  const init = await PSI(() =>
    createLoader(async () => {
      const addon = { exports: {} }
      process.dlopen(addon, nativeAddonPath)
      return addon.exports as psi.Library
    })
  )
  return init({ client: true, server: true })
}

type Round = {
  readonly client: Client
  readonly setup: Uint8Array
  readonly response: Uint8Array
  readonly responseCount: number
}

// A round over `serverCount` server inputs and `clientCount` client inputs
// drawn with overlap and with repeats, so the response holds equal elements.
const makeRound = (
  psiLib: PSILibrary,
  reveal: boolean,
  serverCount: number,
  clientCount: number
): Round => {
  const server = psiLib.server!.createWithNewKey(reveal)
  const client = psiLib.client!.createWithNewKey(reveal)
  const universe = Math.max(1, serverCount + clientCount)
  let state = 12345
  const next = (): number => {
    state = (state * 1103515245 + 12345) % 2147483648
    return state % universe
  }
  const serverInputs = Array.from(
    { length: serverCount },
    (_, i) => `Element ${i}`
  )
  const clientInputs = Array.from(
    { length: clientCount },
    () => `Element ${next()}`
  )
  const setup = server.createSetupMessage(
    0,
    -1,
    serverInputs,
    psiLib.dataStructure.Raw
  )
  const response = server.processRequest(client.createRequest(clientInputs))
  server.delete()
  return {
    client,
    setup: setup.serializeBinary(),
    response: response.serializeBinary(),
    responseCount: response.getEncryptedElementsList().length
  }
}

const pieces = (bytes: Uint8Array, size: number): Uint8Array[] => {
  if (size === 0) return [bytes]
  const out: Uint8Array[] = []
  for (let at = 0; at < bytes.length; at += size)
    out.push(bytes.subarray(at, at + size))
  return out
}

const runMatch = (
  client: Client,
  setup: Uint8Array,
  response: Uint8Array,
  setupPiece = 0,
  responsePiece = 0
): MatchResult => {
  const match = client.createMatch()
  try {
    for (const piece of pieces(setup, setupPiece)) match.addSetupBytes(piece)
    match.sealSetup()
    for (const piece of pieces(response, responsePiece))
      match.matchResponsePiece(piece)
    return match.finish()
  } finally {
    match.delete()
  }
}

const sortedPairs = (
  responseIndices: ArrayLike<number>,
  setupIndices: ArrayLike<number>
): Array<[number, number]> =>
  Array.from(responseIndices, (r, i): [number, number] => [
    r,
    setupIndices[i]
  ]).sort((a, b) => a[0] - b[0] || a[1] - b[1])

const libraryTable = (
  psiLib: PSILibrary,
  round: Round,
  setup: Uint8Array = round.setup
): Array<[number, number]> => {
  const table = round.client.getAssociationTable(
    psiLib.serverSetup.deserializeBinary(setup),
    psiLib.response.deserializeBinary(round.response)
  )
  return sortedPairs(table[0], table[1])
}

const librarySize = (
  psiLib: PSILibrary,
  round: Round,
  setup: Uint8Array = round.setup
): number =>
  round.client.getIntersectionSize(
    psiLib.serverSetup.deserializeBinary(setup),
    psiLib.response.deserializeBinary(round.response)
  )

const rawSetup = (elements: readonly Uint8Array[]): Uint8Array => {
  const setup = new ServerSetup()
  const raw = new ServerSetup.RawInfo()
  raw.setEncryptedElementsList([...elements])
  setup.setRaw(raw)
  return setup.serializeBinary()
}

const compareBytes = (a: Uint8Array, b: Uint8Array): number => {
  for (let i = 0; i < Math.min(a.length, b.length); i += 1)
    if (a[i] !== b[i]) return a[i] - b[i]
  return a.length - b.length
}

const backends: Array<[string, () => Promise<PSILibrary>, boolean]> = [
  ['wasm', PSIWasm, true],
  ['native', loadNative, haveNative || !!process.env.PSI_REQUIRE_NATIVE]
]

describe.each(backends)('PSI Match (%s)', (_name, load, enabled) => {
  const run = enabled ? test : test.skip
  let psiLib: PSILibrary
  beforeAll(async () => {
    if (enabled) psiLib = await load()
  })

  run('equals the library association table and size', () => {
    for (const [serverCount, clientCount] of [
      [0, 0],
      [0, 5],
      [5, 0],
      [100, 60],
      [500, 800]
    ]) {
      const round = makeRound(psiLib, true, serverCount, clientCount)
      const result = runMatch(round.client, round.setup, round.response)
      expect(result.associationTable).toBeDefined()
      const [responseIndices, setupIndices] = result.associationTable!
      // By tag: the native addon's arrays come from outside Jest's realm.
      expect(Object.prototype.toString.call(responseIndices)).toBe(
        '[object Uint32Array]'
      )
      expect(sortedPairs(responseIndices, setupIndices)).toEqual(
        libraryTable(psiLib, round)
      )
      expect(result.intersectionSize).toBe(librarySize(psiLib, round))
      expect(result.decryptedCount).toBe(round.responseCount)
      round.client.delete()
    }
  })

  run('counts without recording pairs when not revealing', () => {
    const round = makeRound(psiLib, false, 300, 400)
    const result = runMatch(round.client, round.setup, round.response, 0, 35)
    expect(result.associationTable).toBeUndefined()
    expect(result.intersectionSize).toBe(librarySize(psiLib, round))
    expect(result.decryptedCount).toBe(round.responseCount)
    round.client.delete()
  })

  run('gives the same result however the bytes are cut', () => {
    const round = makeRound(psiLib, true, 200, 150)
    const whole = runMatch(round.client, round.setup, round.response)
    expect(whole.intersectionSize).toBeGreaterThan(0)
    for (const size of [1, 7, 34, 35, 36, 1000]) {
      const cut = runMatch(
        round.client,
        round.setup,
        round.response,
        size,
        size
      )
      expect(cut).toEqual(whole)
    }
    round.client.delete()
  })

  run('matches a setup larger than one copy window', () => {
    // 40,000 filler elements put the setup past 1 MiB.
    const round = makeRound(psiLib, true, 200, 300)
    const real = psiLib.serverSetup
      .deserializeBinary(round.setup)
      .getRaw()!
      .getEncryptedElementsList_asU8()
    const filler = Array.from({ length: 40000 }, (_, i) =>
      Uint8Array.from([5, ...createHash('sha256').update(`${i}`).digest()])
    )
    const elements = [...real, ...filler].sort(compareBytes)
    const setup = rawSetup(elements)
    expect(setup.length).toBeGreaterThan(1 << 20)
    const result = runMatch(round.client, setup, round.response)
    const [responseIndices, setupIndices] = result.associationTable!
    expect(sortedPairs(responseIndices, setupIndices)).toEqual(
      libraryTable(psiLib, round, setup)
    )
    expect(result.intersectionSize).toBe(librarySize(psiLib, round, setup))
    round.client.delete()
  })

  run('refuses a setup that is not strictly ascending', () => {
    const round = makeRound(psiLib, true, 3, 0)
    const elements = psiLib.serverSetup
      .deserializeBinary(round.setup)
      .getRaw()!
      .getEncryptedElementsList_asU8()
    const match = round.client.createMatch()
    expect(() =>
      match.addSetupBytes(rawSetup([elements[0], elements[2], elements[1]]))
    ).toThrow('strictly ascending')
    // A refused match stays refused.
    expect(() => match.sealSetup()).toThrow('strictly ascending')
    match.delete()
    round.client.delete()
  })

  run('refuses trailing bytes and a wrong tag', () => {
    const round = makeRound(psiLib, true, 3, 3)
    const trailing = new Uint8Array(round.setup.length + 2)
    trailing.set(round.setup)
    trailing.set([0x0a, 0x00], round.setup.length)
    const match = round.client.createMatch()
    expect(() => match.addSetupBytes(trailing)).toThrow()
    match.delete()

    const wrongTag = round.client.createMatch()
    expect(() => wrongTag.addSetupBytes(Uint8Array.from([0x12, 0x00]))).toThrow(
      'not a single Raw'
    )
    wrongTag.delete()
    round.client.delete()
  })

  run('refuses calls out of order', () => {
    const round = makeRound(psiLib, true, 5, 5)
    const early = round.client.createMatch()
    expect(() => early.matchResponsePiece(round.response)).toThrow(
      'before SealSetup'
    )
    early.delete()

    const match = round.client.createMatch()
    match.addSetupBytes(round.setup)
    match.sealSetup()
    expect(() => match.addSetupBytes(round.setup)).toThrow('after SealSetup')
    match.delete()

    const finished = round.client.createMatch()
    finished.addSetupBytes(round.setup)
    finished.sealSetup()
    finished.matchResponsePiece(round.response)
    finished.finish()
    expect(() => finished.matchResponsePiece(round.response)).toThrow(
      'after Finish'
    )
    finished.delete()
    round.client.delete()
  })

  run('refuses use after delete and outlives its client', () => {
    const round = makeRound(psiLib, true, 50, 50)
    const expected = libraryTable(psiLib, round)
    const match = round.client.createMatch()
    round.client.delete()
    match.addSetupBytes(round.setup)
    match.sealSetup()
    match.matchResponsePiece(round.response)
    const [responseIndices, setupIndices] = match.finish().associationTable!
    expect(sortedPairs(responseIndices, setupIndices)).toEqual(expected)
    match.delete()
    expect(() => match.sealSetup()).toThrow(ERROR_INSTANCE_DELETED)
    expect(() => match.delete()).toThrow(ERROR_INSTANCE_DELETED)
  })
})
