#!/usr/bin/env bash
# Checks that a Linux LYNX binary runs on the oldest supported distribution (default: Ubuntu 22.04, glibc 2.35):
# no glibc symbol version above MAX_GLIBC, and no dependency on the system's C++ runtime (libstdc++/libgcc_s are
# linked statically, see CMakeLists.txt). The AppImage catalog tests on the oldest Ubuntu LTS still supported.
#
# usage: scripts/check-linux-compat.sh <binary> [max glibc, default 2.35]
set -euo pipefail
BIN="$1"; MAX="${2:-2.35}"
need="$(objdump -T "$BIN" | grep -o 'GLIBC_[0-9][0-9.]*' | sed 's/GLIBC_//' | sort -uV)"
top="$(tail -1 <<< "$need")"
echo "glibc symbol versions: $(tr '\n' ' ' <<< "$need")"
echo "highest: GLIBC_$top (allowed: GLIBC_$MAX)"
fail=0
if [[ "$(printf '%s\n%s\n' "$top" "$MAX" | sort -V | tail -1)" != "$MAX" ]]; then
    echo "ERROR: needs GLIBC_$top, newer than GLIBC_$MAX; these symbols:"
    objdump -T "$BIN" | grep -o 'GLIBC_[0-9][0-9.]*.*' | tr -d ')' | while read -r v s; do
        [[ "$(printf '%s\n%s\n' "${v#GLIBC_}" "$MAX" | sort -V | tail -1)" != "$MAX" ]] && echo "  $s $v" || true
    done
    fail=1
fi
deps="$(objdump -p "$BIN" | awk '/NEEDED/ {print $2}')"
echo "shared libraries: $(tr '\n' ' ' <<< "$deps")"
if grep -qE '^lib(stdc\+\+|gcc_s)\.so' <<< "$deps"; then
    echo "ERROR: links the system's C++ runtime (libstdc++/libgcc_s); link it statically"; fail=1
fi
if objdump -T "$BIN" | grep -qE '(GLIBCXX|CXXABI)_[0-9]'; then
    echo "ERROR: imports GLIBCXX/CXXABI symbols from the system's libstdc++"; fail=1
fi
[[ $fail == 0 ]] && echo "OK: runs with glibc $MAX and no system C++ runtime"
exit $fail
