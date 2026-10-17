#!/usr/bin/env bash
# Cross build of the LYNX ASIO driver (lynx-asio.dll, Windows x64) with Zig (clang + MinGW-w64). REA round 97.
#
#   scripts/build-lynx-asio.sh            -> $OUT/lynx-asio.dll (OUT defaults to build-win)
#
# Install for other programs: copy the DLL somewhere permanent and run, as administrator,
#   regsvr32 "C:\path\to\lynx-asio.dll"        (regsvr32 /u ... removes it)
set -euo pipefail
cd "$(dirname "$0")/.."
ZIG="${ZIG:-$HOME/opt/zig-x86_64-linux-0.14.1/zig}"
OUT="${OUT:-build-win}"
mkdir -p "$OUT"
"$ZIG" c++ -target x86_64-windows-gnu -std=c++20 -O2 -DNDEBUG=1 -fno-sanitize=undefined -Wall -Wextra \
    -DUNICODE -D_UNICODE -Ithird_party/asiosdk/common \
    -shared -o "$OUT/lynx-asio.dll" asio/LynxAsio.cpp asio/lynx-asio.def \
    -lole32 -luuid -lavrt -luser32 -lgdi32 -ladvapi32 -static
echo "built $OUT/lynx-asio.dll"
