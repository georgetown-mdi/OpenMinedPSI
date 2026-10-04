// Measures the streaming match's peak WebAssembly memory on one large round.
//
//   node --max-old-space-size=16384 match-peak.mjs generate <addon> <dir> <N> <reveal|count>
//     Builds the round with the native addon: N server and N client inputs,
//     the same values, fixed keys. Writes setup.bin, response.bin and, when
//     revealing, permutation.bin (the setup's sorting permutation, uint32 LE).
//
//   node match-peak.mjs run <psi_wasm_node.js> <dir> <N> <reveal|count> [pieceMiB]
//     Runs the match on the WebAssembly module, feeding both messages from
//     disk in pieces, and prints one RESULT line: linear memory after each
//     phase, peak RSS, peak JS heap, timings, and whether the result is the
//     expected one (every element matched, each pair the permutation's).

import fs from 'node:fs'
import path from 'node:path'
import { createRequire } from 'node:module'
import { performance } from 'node:perf_hooks'
import v8 from 'node:v8'

const require = createRequire(import.meta.url)
const [command, modulePath, dir, nText, mode, pieceText] = process.argv.slice(2)
const N = Number(nText)
const reveal = mode === 'reveal'
const clientKey = Uint8Array.from({ length: 32 }, (_, i) => i)
const serverKey = Uint8Array.from({ length: 32 }, (_, i) => i + 1)
const started = performance.now()
const log = message =>
  process.stderr.write(
    `${((performance.now() - started) / 1000).toFixed(1)} s ${message}\n`
  )
const value = ({ Value, Status }) => {
  if (Status) throw new Error(Status.Message)
  return Value
}

function generate() {
  const addon = { exports: {} }
  process.dlopen(addon, path.resolve(modulePath))
  const lib = addon.exports
  fs.mkdirSync(dir, { recursive: true })
  const inputs = Array.from({ length: N }, (_, i) => `element-${i}`)
  const server = value(lib.PsiServer.CreateFromKey(serverKey, reveal))
  const client = value(lib.PsiClient.CreateFromKey(clientKey, reveal))
  log('setup')
  const setup = server.CreateSetupMessage(0, -1, inputs, 0, reveal)
  fs.writeFileSync(path.join(dir, 'setup.bin'), value(setup))
  if (reveal) {
    fs.writeFileSync(
      path.join(dir, 'permutation.bin'),
      Uint32Array.from(setup.Permutation)
    )
  }
  log('request')
  const request = value(client.CreateRequest(inputs))
  log('response')
  fs.writeFileSync(
    path.join(dir, 'response.bin'),
    value(server.ProcessRequest(request))
  )
  log('done')
}

async function run() {
  // The engine's linear memory is an export of the instance the module
  // creates; capture it as the instance is made.
  let memory
  const capture = result => {
    const instance = result.instance ?? result
    for (const exported of Object.values(instance.exports ?? {}))
      if (exported instanceof WebAssembly.Memory) memory ??= exported
    return result
  }
  const instantiate = WebAssembly.instantiate
  WebAssembly.instantiate = (...args) => instantiate(...args).then(capture)
  const lib = await require(path.resolve(modulePath))()
  WebAssembly.instantiate = instantiate
  if (!memory) throw new Error('no WebAssembly.Memory export found')

  const pieceBytes = Number(pieceText ?? 16) * 2 ** 20
  const piece = Buffer.alloc(pieceBytes)
  let heapPeak = 0
  const sample = () => {
    heapPeak = Math.max(heapPeak, v8.getHeapStatistics().used_heap_size)
  }
  const feed = (file, call) => {
    const fd = fs.openSync(path.join(dir, file), 'r')
    let total = 0
    for (;;) {
      const read = fs.readSync(fd, piece, 0, pieceBytes, null)
      if (read === 0) break
      value(call(piece.subarray(0, read)))
      total += read
      sample()
    }
    fs.closeSync(fd)
    return total
  }

  const client = value(lib.PsiClient.CreateFromKey(clientKey, reveal))
  const match = value(client.CreateMatch())
  const memoryAtStart = memory.buffer.byteLength
  log(`setup (memory ${memoryAtStart})`)
  const t0 = performance.now()
  const setupBytes = feed('setup.bin', bytes => match.AddSetupBytes(bytes))
  value(match.SealSetup())
  const setupMs = performance.now() - t0
  const memoryAfterSetup = memory.buffer.byteLength
  log(`response (memory ${memoryAfterSetup})`)
  const t1 = performance.now()
  let responseBytes = 0
  {
    const fd = fs.openSync(path.join(dir, 'response.bin'), 'r')
    for (;;) {
      const read = fs.readSync(fd, piece, 0, pieceBytes, null)
      if (read === 0) break
      value(match.MatchResponsePiece(piece.subarray(0, read)))
      responseBytes += read
      sample()
      log(
        `  ${responseBytes} response bytes (memory ${memory.buffer.byteLength})`
      )
    }
    fs.closeSync(fd)
  }
  const matchMs = performance.now() - t1
  const memoryAfterResponse = memory.buffer.byteLength
  const finished = value(match.Finish())
  sample()
  const memoryAfterFinish = memory.buffer.byteLength
  match.delete()
  client.delete()

  let pairsExpected = null
  if (reveal) {
    const permutation = new Uint32Array(
      fs.readFileSync(path.join(dir, 'permutation.bin')).buffer.slice(0)
    )
    const { ResponseIndices, SetupIndices } = finished
    pairsExpected = ResponseIndices.length === N
    for (let k = 0; pairsExpected && k < ResponseIndices.length; k += 1)
      pairsExpected = permutation[SetupIndices[k]] === ResponseIndices[k]
  }
  const result = {
    N,
    mode,
    node: process.version,
    pieceBytes,
    setupBytes,
    responseBytes,
    memoryAtStart,
    memoryAfterSetup,
    memoryAfterResponse,
    memoryAfterFinish,
    peakRssBytes: process.resourceUsage().maxRSS * 1024,
    peakJsHeapUsedBytes: heapPeak,
    setupMs: Math.round(setupMs),
    matchMs: Math.round(matchMs),
    decryptedCount: finished.DecryptedCount,
    intersectionSize: finished.IntersectionSize,
    pairs: finished.ResponseIndices?.length ?? null,
    pairsExpected,
    expected:
      finished.DecryptedCount === N &&
      finished.IntersectionSize === N &&
      pairsExpected !== false
  }
  console.log(`RESULT ${JSON.stringify(result)}`)
}

if (command === 'generate') generate()
else if (command === 'run') await run()
else {
  console.error('usage: match-peak.mjs generate|run ...')
  process.exit(2)
}
