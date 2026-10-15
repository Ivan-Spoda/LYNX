#!/usr/bin/env bash
# Checks that this machine is set up like docs/SETUP_DEV.md describes. Prints one line per check; exit 1 on failure.
# Usage: scripts/verify-env.sh [FL install dir]
set -u
FL="${1:-/mnt/c/Program Files/Image-Line/FL Studio 2026}"
fails=0
ok () { printf '  ok    %s\n' "$1"; }
bad () { printf '  FAIL  %s\n' "$1"; fails=$((fails + 1)); }
warn () { printf '  warn  %s\n' "$1"; }
have () { [[ -x "$1" ]] && ok "$2" || bad "$2 (missing: $1)"; }

echo "Tools"
have "${ZIG_LOCAL:-$HOME/opt/zig-linux-x86_64-0.13.0/zig}" "zig 0.13.0 (tests)"
have "${ZIG:-$HOME/opt/zig-x86_64-linux-0.14.1/zig}" "zig 0.14.1 (Windows build)"
[[ -d "${JUCE_DIR:-$HOME/opt/juce-8.0.4}/modules" ]] && ok "JUCE 8.0.4" || bad "JUCE 8.0.4 (JUCE_DIR)"
command -v python3 >/dev/null && python3 -c 'import numpy' 2>/dev/null && ok "python3 + numpy" || bad "python3 + numpy"
command -v git >/dev/null && ok "git" || bad "git"
command -v gh >/dev/null || ls "$HOME"/opt/gh_*/bin/gh >/dev/null 2>&1 && ok "gh" || warn "gh (only needed to publish)"
ls -d "$HOME"/opt/ghidra_* >/dev/null 2>&1 && ok "Ghidra" || warn "Ghidra (only needed for REA work)"
ls -d "$HOME"/opt/jdk-21* >/dev/null 2>&1 && ok "JDK 21" || warn "JDK 21 (only needed for REA work)"
grep -q '"rea"' "$HOME/.claude.json" 2>/dev/null && ok "REA MCP registered" || warn "REA MCP not registered in ~/.claude.json"

echo "Machine"
mem=$(awk '/MemTotal/ { printf "%d", $2 / 1048576 }' /proc/meminfo)
(( mem >= 8 )) && ok "WSL memory ${mem} GB" || warn "WSL memory ${mem} GB (raise it in .wslconfig)"
ok "$(nproc) logical cores"

echo "FL Studio installation: $FL"
if [[ -d "$FL/Data" ]]; then
    ok "Data folder"
    # files LYNX reads at run time and checks by SHA-256 (the decoded builds)
    check () {
        local f="$FL/$1" want="$2"
        if [[ ! -f "$f" ]]; then bad "$1 missing"; return; fi
        [[ "$(sha256sum "$f" | cut -c1-64)" == "$want" ]] && ok "$1" || warn "$1 differs from the decoded build (engine falls back)"
    }
    check "Plugins/Fruity/Generators/BooBass/BooBass_Eng_x64.dll" 4e10efcd9f945860911e7cdff9c4065c4f193b58ddffc8304b45add1db51705f
    check "FLEngine_x64.dll" 6d9c5ab09c1d5594012ac2c31cf9ed08e73f42164ed0964cccae10a95d9b8e02
    check "Shared/SG_x64.dll" ad567e65e2aac8314d23f2322f4d17e1b5529a09f64c43d596189582f006f6ac
    check "Shared/Reverb_x64.dll" 9881ff2ccd74227e454a3d5f1912568f4336abe06e68f3693162aa4d251c969f
    n=$(find "$FL/Data/Demo projects" -name '*.flp' 2>/dev/null | wc -l)
    (( n > 0 )) && ok "$n demo projects" || bad "no demo projects"
else
    bad "FL Studio not found (pass its folder as the first argument)"
fi

echo
(( fails == 0 )) && echo "Environment OK" || echo "$fails check(s) failed"
exit $(( fails > 0 ))
