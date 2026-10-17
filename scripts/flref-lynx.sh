#!/usr/bin/env bash
# flref-lynx.sh — renders the same projects as flref-fl.sh with LYNX (build-local/renderwav, whole song, float raw)
# usage: flref-lynx.sh <list-of-flp-paths> <out-dir> [renderwav] [jobs]   (run from the repo root)
#   writes <out-dir>/<group>/<name>.raw (float32 L block then R block) and <name>.log (renderwav's channel listing)
# env: FL_ROOT as in flref-fl.sh; LYNX_SOLO etc. are passed through to renderwav.
set -u
LIST="$1"; OUT="$2"
RW="${3:-build-local/renderwav}"; RW=$(realpath "$RW")
JOBS="${4:-6}"
export FL_ROOT="${FL_ROOT:-/mnt/c/Program Files/Image-Line/FL Studio 2026}" OUT RW
one () {
    flp="$1"
    rel="${flp#"$FL_ROOT/Data/"}"
    group=$(dirname "$rel" | tr ' /' '_-')
    name=$(basename "$flp" .flp)
    mkdir -p "$OUT/$group"
    [[ -s "$OUT/$group/$name.raw" ]] && return 0
    start=$(date +%s.%N)
    LYNX_RAW="$OUT/$group/$name.raw" timeout 1800 "$RW" "$flp" /dev/null song > "$OUT/$group/$name.log" 2>&1
    st=$?
    printf '%s\t%d\t%.1f\t%s\n' "$(date +%T)" "$st" "$(awk -v a="$start" -v b="$(date +%s.%N)" 'BEGIN{print b-a}')" \
        "$group/$name" >> "$OUT/render.log"
}
export -f one
tr '\n' '\0' < "$LIST" | xargs -0 -P "$JOBS" -I{} bash -c 'one "$1"' _ {}
