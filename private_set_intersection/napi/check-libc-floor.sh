#!/usr/bin/env bash
#
# Build-time gate for the native N-API prebuilds. It asserts the POSITIVE shape
# of a loadable, self-contained addon rather than denylisting known-bad patterns,
# so a new failure mode cannot slip through an unenumerated case. Fails the build
# unless the freshly built .node is:
#
#   - an ELF shared object (ET_DYN) that exports napi_register_module_v1 -- i.e.
#     actually a loadable Node addon, not a static executable or a stray object;
#   - free of any dynamic C++ runtime: no GLIBCXX_/CXXABI_ versioned symbols; no
#     re-exported operator new/delete (which would let the host libstdc++
#     interpose the addon's statically linked libc++ -- the `free(): invalid
#     pointer` crash); and no UNDEFINED operator new/delete / __cxa_* / _Unwind_*
#     / typeinfo (which, if libc++/libc++abi/libunwind stopped being static, would
#     resolve from the host runtime at load -- the same mismatch in reverse);
#   - linked only against its own libc: every NEEDED entry is in a small per-libc
#     allowlist, so libstdc++/libc++/libgcc_s/libunwind or any other runtime (and,
#     on the musl leg, a glibc soname) are rejected by construction;
#   - within the glibc floor (glibc leg), or free of any glibc reference (musl).
#
# The floor is enforced here, not assumed from the runner, because the hermetic
# zig toolchain -- not the runner's glibc -- determines it; this is the drift that
# shipped a GLIBC_2.38 addon before.
#
# Usage: check-libc-floor.sh <addon.node> <glibc|musl> [max_glibc=2.28]

set -euo pipefail

f="${1:?usage: check-libc-floor.sh <addon.node> <glibc|musl> [max_glibc]}"
libc="${2:?expected libc: glibc or musl}"
max="${3:-2.28}"
fail=0
say() { echo "  $*"; }

echo "== libc-floor check: $f (expect $libc, glibc floor $max) =="

# The checks below parse readelf output and absorb grep's no-match exit with
# `|| true`. Assert first that the artifact is a shared object, so a truncated,
# non-ELF, static-executable, or relocatable file fails loudly here instead of
# yielding empty symbol/NEEDED sets that pass every check silently.
if ! readelf -h "$f" >/dev/null 2>&1; then
  echo "== FAIL: $f is not a readable ELF object =="; exit 1
fi
if [ "$(readelf -h "$f" | awk '/^ *Type:/ {print $2; exit}')" != "DYN" ]; then
  echo "== FAIL: $f is not an ELF shared object (ET_DYN) =="; exit 1
fi

syms=$(readelf --dyn-syms --wide "$f" | grep -oE 'GLIBC_[0-9.]+|GLIBCXX_[0-9.]+|CXXABI_[0-9.]+' | sort -Vu || true)
needed=$(readelf -d "$f" | awk '/NEEDED/ {print $NF}' | tr -d '[]' || true)
exported=$(readelf --dyn-syms --wide "$f" | awk '$7 != "UND" && ($5 == "GLOBAL" || $5 == "WEAK") {print $8}' | sed 's/@.*//' | grep -v '^$' | sort -u || true)
undef=$(readelf --dyn-syms --wide "$f" | awk '$7 == "UND" {print $8}' | sed 's/@.*//' | grep -v '^$' | sort -u || true)

# It must actually be a Node addon.
if ! echo "$exported" | grep -qx 'napi_register_module_v1'; then
  say "FAIL: does not export napi_register_module_v1 -- not a Node addon"; fail=1
fi

# ...and ONLY that. napi.lds binds every other symbol local (local: *), so the
# addon's statically linked BoringSSL/libc++ can never interpose the host
# process's OpenSSL/libstdc++. Any extra dynamic export -- e.g. the patched
# OPENSSL_thread_stop, or an EVP_/EC_/RAND_ symbol -- means the version script
# regressed and crypto symbols leaked into the process's global namespace.
extra=$(echo "$exported" | grep -vx 'napi_register_module_v1' | grep -v '^$' || true)
if [ -n "$extra" ]; then
  say "FAIL: exports symbols beyond the N-API entry (version script regressed): $(echo "$extra" | paste -sd, -)"; fail=1
fi

# No dynamic C++ runtime -- three ways it could sneak in:
#   (a) versioned libstdc++ symbols;
if echo "$syms" | grep -qE 'GLIBCXX_|CXXABI_'; then
  say "FAIL: dynamic libstdc++ symbols (GLIBCXX_/CXXABI_) present"; fail=1
fi
#   (b) re-exported operator new/delete. Match only the Itanium manglings
#       _Znw/_Zna/_Zdl/_Zda, not the broader ^_Zn/^_Zd (operator!=, operator*, ...).
if echo "$exported" | grep -qE '^_Znw|^_Zna|^_Zdl|^_Zda'; then
  say "FAIL: re-exports C++ operator new/delete -- version script regressed"; fail=1
fi
#   (c) UNDEFINED C++ runtime symbols would resolve from the host at load. Allow
#       only the libc __cxa_ hooks (atexit/finalize), which are shared with the
#       process legitimately.
badundef=$(echo "$undef" \
  | grep -E '^_Znw|^_Zna|^_Zdl|^_Zda|^__cxa_|^_Unwind_|^__gxx_personality|^_ZT[ISV]' \
  | grep -vE '^__cxa_atexit$|^__cxa_finalize$|^__cxa_thread_atexit_impl$' || true)
if [ -n "$badundef" ]; then
  say "FAIL: undefined C++ runtime symbols would resolve from the host: $(echo "$badundef" | paste -sd, -)"; fail=1
fi

# Linked only against its own libc: every NEEDED entry must be in the per-libc
# allowlist. This rejects any C++ or auxiliary runtime by construction, and -- on
# the musl leg -- a glibc soname.
case "$libc" in
  glibc) allow='^(libc|libm|libdl|libpthread|librt)\.so\.[0-9]+$|^ld-linux' ;;
  musl) allow='^libc\.so$|^libc\.musl-|^ld-musl' ;;
  *) say "FAIL: unknown libc '$libc' (expected glibc or musl)"; fail=1; allow='.^' ;;
esac
while IFS= read -r n; do
  [ -z "$n" ] && continue
  if ! echo "$n" | grep -qE "$allow"; then
    say "FAIL: NEEDED '$n' is not a permitted $libc library"; fail=1
  fi
done <<EOF
$needed
EOF

# libc floor.
case "$libc" in
  glibc)
    # `|| true`: grep exits 1 when $syms carries no GLIBC_ line (a GLIBCXX_-only
    # regression, flagged above), which under pipefail would abort before the
    # verdict.
    hi=$(echo "$syms" | grep -oE 'GLIBC_[0-9.]+' | sed 's/GLIBC_//' | sort -V | tail -1 || true)
    if [ -n "$hi" ] && [ "$(printf '%s\n%s\n' "$hi" "$max" | sort -V | tail -1)" != "$max" ]; then
      say "FAIL: requires GLIBC_$hi > floor $max"; fail=1
    else
      say "OK: max GLIBC_${hi:-none} <= $max"
    fi
    ;;
  musl)
    if echo "$syms" | grep -qE 'GLIBC_'; then
      say "FAIL: musl build references glibc versioned symbols: $(echo "$syms" | paste -sd, -)"; fail=1
    else
      say "OK: no glibc symbols"
    fi
    ;;
esac

if [ "$fail" = 0 ]; then
  echo "== PASS: NEEDED = ${needed//$'\n'/ } =="
else
  echo "== FAIL =="
  exit 1
fi
