#!/usr/bin/env bash
# Builds tools/fxmeasure (Windows console VST3 measurement host) with Zig, linking the JUCE module objects of a
# previous scripts/build-windows-zig.sh run (build-win/juce/*.o, same JuceConfig.h). Output: build-win/fxmeasure.exe
# Optional: DEST=<folder> copies the exe there (it must run from a Windows path).
set -euo pipefail
cd "$(dirname "$0")/../.."
ZIG="${ZIG:-$HOME/opt/zig-x86_64-linux-0.14.1/zig}"
JUCE_DIR="${JUCE_DIR:-$HOME/opt/juce-8.0.4}"
OUT="${OUT:-build-win}"
[[ -f "$OUT/JuceConfig.h" && -f "$OUT/juce/juce_audio_processors.o" ]] || { echo "run scripts/build-windows-zig.sh first" >&2; exit 1; }
mkdir -p "$OUT/fxmeasure"
C=(-target x86_64-windows-gnu -std=c++20 -O2 -DNDEBUG=1 -fno-sanitize=undefined -include "$OUT/JuceConfig.h" -include cmake/mingw/mingw_compat.h
   -I"$OUT/shim" -I"$JUCE_DIR/modules" -I"$JUCE_DIR/modules/juce_audio_processors/format_types/VST3_SDK" -Isrc)
"$ZIG" c++ "${C[@]}" -c src/platform/MingwFixes.cpp -o "$OUT/fxmeasure/mingwfixes.o"
"$ZIG" c++ "${C[@]}" -Wall -c tools/fxmeasure/fxmeasure.cpp -o "$OUT/fxmeasure/fxmeasure.o"
# common-controls v6 manifest: without it the process stalls before main (hidden loader error box)
printf '#include <winuser.h>\nCREATEPROCESS_MANIFEST_RESOURCE_ID RT_MANIFEST "LYNX.manifest"\n' > "$OUT/fxmeasure/fx.rc"
cp resources/LYNX.manifest "$OUT/fxmeasure/"
( cd "$OUT/fxmeasure" && "$ZIG" rc /fo fx.res fx.rc )
"$ZIG" c++ -target x86_64-windows-gnu -O2 -o "$OUT/fxmeasure.exe" "$OUT"/juce/*.o "$OUT/fxmeasure/fxmeasure.o" "$OUT/fxmeasure/mingwfixes.o" "$OUT/fxmeasure/fx.res" \
    -Wl,--subsystem,console \
    -lole32 -loleaut32 -luuid -lwinmm -lws2_32 -lshlwapi -lversion -lwininet -limm32 -lcomdlg32 \
    -lgdi32 -luser32 -lcomctl32 -lshell32 -ladvapi32 -lrpcrt4 -ldbghelp -lsetupapi -luxtheme -ldwmapi \
    -ld2d1 -ldcomp -ldwrite -ldxgi -ld3d11 -ldxguid -lwsock32 -lopengl32 -lpropsys -lmfplat -lmfuuid -lksuser -lavrt
[[ -n "${DEST:-}" ]] && cp "$OUT/fxmeasure.exe" "$DEST/"
ls -la "$OUT/fxmeasure.exe"
