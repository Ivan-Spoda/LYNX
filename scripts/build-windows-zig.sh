#!/usr/bin/env bash
# Cross build of LYNX.exe (Windows x64) from Linux/WSL using Zig (clang + MinGW-w64).
# Needs neither Visual Studio nor CMake. The official build is still CMakeLists.txt.
#
# Variables: ZIG (path to zig), JUCE_DIR (JUCE 8 checkout), OUT (output folder), JOBS
set -euo pipefail
cd "$(dirname "$0")/.."

ZIG="${ZIG:-$HOME/opt/zig-x86_64-linux-0.14.1/zig}"
JUCE_DIR="${JUCE_DIR:-$HOME/opt/juce-8.0.4}"
OUT="${OUT:-build-win}"
JOBS="${JOBS:-$(nproc)}"
TARGET=x86_64-windows-gnu

mkdir -p "$OUT/juce" "$OUT/app" "$OUT/shim"

# MinGW headers are lowercase; JUCE includes some with capitals (on Windows it does not matter).
echo '#include <dbghelp.h>' > "$OUT/shim/Dbghelp.h"

MODULES=(juce_core juce_data_structures juce_events juce_graphics juce_gui_basics juce_gui_extra
         juce_audio_basics juce_audio_devices juce_audio_formats juce_audio_processors juce_audio_utils juce_dsp)

{
    echo '#pragma once'
    echo '#define JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED 1'
    echo '#define JUCE_STANDALONE_APPLICATION 1'
    echo '#define JUCE_USE_CURL 0'
    echo '#define JUCE_WEB_BROWSER 0'
    echo '#define JUCE_USE_WINDOWS_MEDIA_FORMAT 0'
    echo '#define JUCE_PLUGINHOST_VST3 1'
    echo '#define JUCE_ASIO 1'
    echo '#define JUCE_USE_MP3AUDIOFORMAT 1'
    echo '#define JUCE_APPLICATION_NAME_STRING "LYNX"'
    echo '#define JUCE_APPLICATION_VERSION_STRING "0.1.2"'
    for m in "${MODULES[@]}"; do echo "#define JUCE_MODULE_AVAILABLE_$m 1"; done
} > "$OUT/JuceConfig.h.new"
# only replace the file if it changed, so the module cache is not invalidated
cmp -s "$OUT/JuceConfig.h.new" "$OUT/JuceConfig.h" 2>/dev/null && rm "$OUT/JuceConfig.h.new" || mv "$OUT/JuceConfig.h.new" "$OUT/JuceConfig.h"

COMMON=(-target "$TARGET" -std=c++20 -O2 -DNDEBUG=1 -fno-sanitize=undefined
        -include "$OUT/JuceConfig.h" -include cmake/mingw/mingw_compat.h
        -I"$OUT/shim" -I"$JUCE_DIR/modules" -I"$JUCE_DIR/modules/juce_audio_processors/format_types/VST3_SDK" -Ithird_party/clap/include -Ithird_party/wavpack/include -Ithird_party/asiosdk/common -Isrc)

export ZIG OUT JUCE_DIR TARGET
export COMMON_STR="${COMMON[*]}"

echo "Compiling JUCE modules (cached)..."
for m in "${MODULES[@]}"; do echo "$m"; done | xargs -P "$JOBS" -I{} bash -c '
    m={}; obj="$OUT/juce/$m.o"
    if [[ ! -f "$obj" || "$OUT/JuceConfig.h" -nt "$obj" ]]; then
        echo "  JUCE $m"
        extra=""
        # JUCE ASIO uses MSVC __try/__except: clang accepts it with -fms-extensions, but on MinGW it does not catch
        # (a driver crashing while being opened/closed is not contained in this build; the MSVC build keeps the guard)
        [[ "$m" == juce_audio_devices ]] && extra="-fms-extensions -DJUCE_DONT_AUTOLINK_TO_WIN32_LIBRARIES=1"
        $ZIG c++ $COMMON_STR $extra -w -c "$JUCE_DIR/modules/$m/$m.cpp" -o "$obj"
    fi'

# Extra units that JUCE's CMake compiles along with the modules
for f in juce_core/juce_core_CompilationTime.cpp juce_graphics/juce_graphics_Harfbuzz.cpp; do
    obj="$OUT/juce/$(basename "$f" .cpp).o"
    if [[ ! -f "$obj" || "$OUT/JuceConfig.h" -nt "$obj" ]]; then
        echo "  JUCE $(basename "$f")"
        "$ZIG" c++ $COMMON_STR -w -c "$JUCE_DIR/modules/$f" -o "$obj"
    fi
done
obj="$OUT/juce/juce_graphics_Sheenbidi.o"
if [[ ! -f "$obj" ]]; then
    echo "  JUCE juce_graphics_Sheenbidi.c"
    "$ZIG" cc -target "$TARGET" -O2 -w -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 -I"$JUCE_DIR/modules" -c "$JUCE_DIR/modules/juce_graphics/juce_graphics_Sheenbidi.c" -o "$obj"
fi

APP_SOURCES=(src/Main.cpp
             src/engine/AudioEngine.cpp src/engine/ChannelRackProcessor.cpp src/engine/Metronome.cpp
             src/engine/SampleLibrary.cpp src/engine/WavPackFormat.cpp src/engine/ProjectFile.cpp src/engine/PluginHost.cpp src/engine/PluginSandbox.cpp src/engine/Exporter.cpp src/engine/StreamPrefetcher.cpp src/engine/ClapFormat.cpp src/engine/MidiFileIo.cpp src/engine/PreviewPlayer.cpp
             src/ui/MainComponent.cpp src/ui/ChannelRackComponent.cpp src/ui/PianoRollComponent.cpp src/ui/PlaylistComponent.cpp src/ui/MixerComponent.cpp src/ui/PluginWindow.cpp src/ui/WindowOwner.cpp src/ui/ParametricEqEditor.cpp src/ui/ExportDialog.cpp src/ui/AutomationUi.cpp src/ui/ChannelSettings.cpp src/ui/BrowserComponent.cpp src/ui/Theme.cpp src/ui/Icons.cpp src/ui/I18n.cpp src/ui/EditorTools.cpp
             src/core/Song.cpp src/core/Scheduler.cpp src/core/Instruments.cpp src/core/RackRenderer.cpp
             src/core/FlpImport.cpp src/core/Effects.cpp src/core/NoteEdit.cpp src/core/Recorder.cpp src/core/Pdc.cpp src/core/WorkerPool.cpp src/core/TimeStretch.cpp src/core/Arpeggiator.cpp src/core/SynthPresets.cpp src/core/Slicer.cpp src/core/Templates.cpp src/core/FlWrapper.cpp src/core/FlEffects.cpp src/core/Effector.cpp src/core/VintagePhaser.cpp src/core/Vocodex.cpp src/core/MaximusFx.cpp src/core/FlSamplers.cpp src/core/FlSynths.cpp src/core/FlSytrus.cpp src/core/FlGenerators.cpp src/core/PhysicalModels.cpp src/core/DrumSynthLive.cpp src/core/SytrusEngine.cpp src/core/Harmless.cpp src/core/HarmlessFx.cpp src/core/Harmor.cpp src/core/FlPatcher.cpp src/core/Inflate.cpp src/core/WavPack.cpp src/flp/Flp.cpp
             src/platform/MingwFixes.cpp)

# workflow shell: core (key map, workspace layout, rack operations) and UI (workspace desktop, control contract)
APP_SOURCES+=(src/core/KeyMap.cpp src/core/Workspace.cpp src/core/RackOps.cpp src/ui/Workspace.cpp src/ui/ValueControl.cpp src/ui/Dialogs.cpp)
# Windows file association (.lynx) and its icon
APP_SOURCES+=(src/platform/FileTypes.cpp)
# start-up: splash screen and its procedurally synthesised sound
APP_SOURCES+=(src/core/StartupSound.cpp src/ui/Splash.cpp)

echo "WavPack (decoder, C)..."
mkdir -p "$OUT/wavpack"
for f in third_party/wavpack/src/*.c; do
    obj="$OUT/wavpack/$(basename "$f" .c).o"
    if [[ ! -f "$obj" || "$f" -nt "$obj" ]]; then
        "$ZIG" cc -target "$TARGET" -O2 -w -DHAVE___BUILTIN_CLZ=1 -Ithird_party/wavpack/include -Ithird_party/wavpack/src -c "$f" -o "$obj"
    fi
done

echo "Compiling the app..."
printf '%s\n' "${APP_SOURCES[@]}" | xargs -P "$JOBS" -I{} bash -c '
    src={}; obj="$OUT/app/$(echo "$src" | tr / _).o"
    $ZIG c++ $COMMON_STR -Wall -Wextra -c "$src" -o "$obj"'

echo "Resources (manifest)..."
( cd resources && "$ZIG" rc /fo "../$OUT/LYNX.res" LYNX.rc )

echo "Linking..."
"$ZIG" c++ -target "$TARGET" -O2 -o "$OUT/LYNX.exe" "$OUT"/juce/*.o "$OUT"/app/*.o "$OUT"/wavpack/*.o "$OUT/LYNX.res" \
    -Wl,--subsystem,windows \
    -lole32 -loleaut32 -luuid -lwinmm -lws2_32 -lshlwapi -lversion -lwininet -limm32 -lcomdlg32 \
    -lgdi32 -luser32 -lcomctl32 -lshell32 -ladvapi32 -lrpcrt4 -ldbghelp -lsetupapi -luxtheme -ldwmapi \
    -ld2d1 -ldcomp -ldwrite -ldxgi -ld3d11 -ldxguid -lwsock32 -lopengl32 -lpropsys -lmfplat -lmfuuid -lksuser -lavrt

ls -la "$OUT/LYNX.exe"
