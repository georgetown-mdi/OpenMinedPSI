#!/usr/bin/env bash
#
# Build-time gate for the native N-API prebuilds. Fails the build if a freshly
# built .node would not load on the platforms psilink targets:
#
#   1. It carries a dynamic C++ runtime dependency (GLIBCXX_/CXXABI_ versioned
#      symbols, or libstdc++/libc++/libgcc_s in NEEDED). The zig cc build links
#      libc++ statically, so any such dependency is a regression that would raise
#      the runtime floor (e.g. RHEL/Alma 8 lack GLIBCXX_3.4.32).
#   2. (glibc) It requires a GLIBC_ symbol newer than the declared floor -- the
#      exact drift that shipped a GLIBC_2.38 addon before. The floor is enforced
#      here, not assumed from the runner, because the hermetic zig toolchain --
#      not the runner's glibc -- determines it.
#   3. (musl) It references any glibc versioned symbol at all.
#   4. It re-exports C++ operator new/delete. Only napi_register_module_v1 (plus
#      linker-generated __start/__stop markers) should be exported; re-exporting
#      the C++ runtime is what let the host libstdc++ interpose the addon's libc++
#      and crash it with `free(): invalid pointer`. Guards that regression.
#
# Usage: check-libc-floor.sh <addon.node> <glibc|musl> [max_glibc=2.28]

set -euo pipefail

f="${1:?usage: check-libc-floor.sh <addon.node> <glibc|musl> [max_glibc]}"
libc="${2:?expected libc: glibc or musl}"
max="${3:-2.28}"
fail=0
say() { echo "  $*"; }

syms=$(readelf --dyn-syms --wide "$f" | grep -oE 'GLIBC_[0-9.]+|GLIBCXX_[0-9.]+|CXXABI_[0-9.]+' | sort -Vu || true)
needed=$(readelf -d "$f" | awk '/NEEDED/ {print $NF}' | tr -d '[]')
exported=$(readelf --dyn-syms --wide "$f" | awk '$7 != "UND" && ($5 == "GLOBAL" || $5 == "WEAK") {print $8}' | grep -v '^$' || true)

echo "== libc-floor check: $f (expect $libc, glibc floor $max) =="

# 1. No dynamic C++ runtime dependency (libc++ must be statically linked).
if echo "$syms" | grep -qE 'GLIBCXX_|CXXABI_'; then
  say "FAIL: dynamic libstdc++ symbols (GLIBCXX_/CXXABI_) present"; fail=1
fi
if echo "$needed" | grep -qiE 'libstdc\+\+|libc\+\+|libgcc_s'; then
  say "FAIL: NEEDED pulls a C++ runtime: $needed"; fail=1
fi

# 4. Interposition-regression guard: never re-export operator new/delete.
if echo "$exported" | grep -qE '^_Zn|^_Zd'; then
  say "FAIL: exports C++ operator new/delete -- version script regressed"; fail=1
fi

case "$libc" in
  glibc)
    hi=$(echo "$syms" | grep -oE 'GLIBC_[0-9.]+' | sed 's/GLIBC_//' | sort -V | tail -1)
    if [ -n "$hi" ] && [ "$(printf '%s\n%s\n' "$hi" "$max" | sort -V | tail -1)" != "$max" ]; then
      say "FAIL: requires GLIBC_$hi > floor $max"; fail=1
    else
      say "OK: max GLIBC_${hi:-none} <= $max"
    fi
    ;;
  musl)
    if echo "$syms" | grep -qE 'GLIBC_'; then
      say "FAIL: musl build references glibc symbols: $(echo "$syms" | paste -sd, -)"; fail=1
    else
      say "OK: no glibc symbols"
    fi
    ;;
  *)
    say "FAIL: unknown libc '$libc' (expected glibc or musl)"; fail=1
    ;;
esac

if [ "$fail" = 0 ]; then
  echo "== PASS: NEEDED = ${needed//$'\n'/ } =="
else
  echo "== FAIL =="
  exit 1
fi
