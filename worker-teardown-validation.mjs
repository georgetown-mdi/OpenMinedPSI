// Validates that the native PSI addon survives worker_threads teardown after a
// crypto op -- the fix for the SIGSEGV that blocks running the native addon off the
// main thread (psilink board item 208035324; see the napi_add_env_cleanup_hook /
// OPENSSL_thread_stop change in private_set_intersection/napi/psi_napi.cpp).
//
// Run in the BUILD environment (Docker build image), against the freshly-built
// native addon:
//
//   node worker-teardown-validation.mjs <path-to-native-loader>
//
// where <path-to-native-loader> is the built module whose DEFAULT export is the
// async loader returning the library (the native counterpart of the vendored
// package's psi_native_node.js, i.e. `const lib = await loadNative()` exposing
// lib.server / lib.client / lib.dataStructure).
//
// BEFORE the fix, every "op" case SIGSEGVs at worker teardown, killing this
// whole process with exit 139 (a segfault on a worker thread is not contained
// to the worker); the "load-only" case is clean. AFTER the fix, every case must
// complete its op and tear down without crashing. Note worker.terminate()
// reports exit code 1 whenever it lands before the worker's natural exit, so a
// case passes on exit 0, or exit 1 iff WE terminated a worker that had already
// reported "done" and no error event fired. This harness itself exits 0 only if
// every case passes.

import {
  Worker,
  isMainThread,
  workerData,
  parentPort,
} from "node:worker_threads";
import { fileURLToPath } from "node:url";

if (!isMainThread) {
  // Worker branch: load the addon, optionally run a crypto op, then let the main
  // thread tear us down (or, for "op-close", close our own port).
  const { mode, loader, count } = workerData;
  const { default: loadNative } = await import(loader);
  const lib = await loadNative();
  if (mode !== "load-only") {
    const inputs = Array.from({ length: count }, (_, i) => `v-${i}`);
    const srv = lib.server.createWithNewKey(true);
    const setup = srv.createSetupMessage(
      0.0,
      -1,
      inputs,
      lib.dataStructure.Raw,
      [],
    );
    const cli = lib.client.createWithNewKey(true);
    const resp = srv.processRequest(cli.createRequest(inputs.slice(1)));
    cli.getAssociationTable(setup, resp);
    if (mode === "op-delete") {
      srv.delete();
      cli.delete();
    }
  }
  parentPort.postMessage("done");
  if (mode === "op-close") parentPort.close();
} else {
  const loader = process.argv[2];
  if (!loader) {
    console.error(
      "usage: node worker-teardown-validation.mjs <path-to-native-loader>",
    );
    process.exit(2);
  }
  const self = fileURLToPath(import.meta.url);

  // Run one worker in the given mode and resolve with its outcome. For every
  // mode except "op-close" the main thread terminates the worker once it
  // signals "done". The pre-fix SIGSEGV kills this whole process (exit 139), so
  // any per-case resolution already means "no crash"; the per-case exit code
  // distinguishes a clean or terminate-raced teardown (0, or 1 after our own
  // terminate) from a worker error (error event, or any other nonzero code).
  const runCase = (mode, count) =>
    new Promise((resolve) => {
      const w = new Worker(self, { workerData: { mode, loader, count } });
      let done = false;
      let terminated = false;
      let errored = false;
      w.on("message", () => {
        done = true;
        if (mode !== "op-close") {
          terminated = true;
          void w.terminate();
        }
      });
      w.on("exit", (code) => resolve({ code, done, terminated, errored }));
      w.on("error", () => {
        errored = true;
      });
    });

  let allPass = true;
  const check = (label, r) => {
    const pass =
      r.done && !r.errored && (r.code === 0 || (r.terminated && r.code === 1));
    allPass &&= pass;
    const detail =
      r.code === 1 && r.terminated ? "exit=1 (terminate raced)" : `exit=${r.code}`;
    console.log(`${pass ? "PASS" : "FAIL"}  ${label.padEnd(28)} ${detail}`);
  };

  // Single small op (below the ~1024-input parallel-threads threshold), each
  // teardown path that reproduced the crash.
  check("load-only + terminate", await runCase("load-only", 3));
  check("op + terminate", await runCase("op", 3));
  check("op + .delete() + terminate", await runCase("op-delete", 3));
  check("op + port.close()", await runCase("op-close", 3));

  // Large op (above the threshold) so the parallel_ec shard threads spawn and their
  // per-shard cipher thread-locals are exercised too.
  check("threaded op + terminate", await runCase("op", 4096));

  // Stress: many op+terminate cycles, to catch a threshold- or timing-dependent
  // variant that a single run would miss.
  let stressPass = true;
  for (let i = 0; i < 25; i++) {
    const r = await runCase("op", 3);
    if (!(r.done && !r.errored && (r.code === 0 || (r.terminated && r.code === 1)))) {
      stressPass = false;
      break;
    }
  }
  allPass &&= stressPass;
  console.log(`${stressPass ? "PASS" : "FAIL"}  stress x25 op + terminate`);

  console.log(
    allPass
      ? "\nALL PASS -- native addon is worker-teardown-safe"
      : "\nFAIL -- teardown still crashes; capture a symbolized backtrace (ASan / gdb) and escalate to a PJC/BoringSSL patch",
  );
  process.exit(allPass ? 0 : 1);
}
