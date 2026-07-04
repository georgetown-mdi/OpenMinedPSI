/* Linker version script for the native N-API addon (Linux/ELF).
 *
 * Node loads the addon with dlopen into a process that has already loaded its
 * own C++ runtime (libstdc++ on Debian/Ubuntu, on Alpine's node too). Our
 * prebuilt .node statically links zig's libc++ instead, so if the addon's
 * `operator new` / `operator delete` (and the rest of its C++ runtime) were
 * exported with default visibility, the dynamic loader would let the process's
 * libstdc++ symbols interpose them. Allocation would then run through one
 * runtime and the matching free through the other -- `free(): invalid pointer`
 * at the first cross-boundary object.
 *
 * Exporting ONLY the Node entry point and binding everything else locally keeps
 * every internal allocation/free inside the addon's own libc++. Node still calls
 * napi_register_module_v1; the addon's imports of napi_* are undefined symbols,
 * unaffected by this script, and are resolved from the Node process as before.
 */
{
  global:
    napi_register_module_v1;
  local:
    *;
};
