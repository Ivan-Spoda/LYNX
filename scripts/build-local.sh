#!/usr/bin/env bash
# Builds and tests the core (without JUCE) with any C++20 compiler.
# Uses $CXX if set; otherwise tries Zig in ~/opt, then g++/clang++.
set -euo pipefail
cd "$(dirname "$0")/.."

if [[ -z "${CXX:-}" ]]; then
    if [[ -x "$HOME/opt/zig-linux-x86_64-0.13.0/zig" ]]; then
        CXX="$HOME/opt/zig-linux-x86_64-0.13.0/zig c++"
    else
        CXX="$(command -v clang++ || command -v g++)"
    fi
fi

FLAGS="-std=c++20 -O2 -g -Wall -Wextra -Wpedantic -Isrc -Ithird_party/wavpack/include"
CORE="src/flp/Flp.cpp src/core/Song.cpp src/core/NoteEdit.cpp src/core/Recorder.cpp src/core/Pdc.cpp src/core/WorkerPool.cpp src/core/TimeStretch.cpp src/core/Arpeggiator.cpp src/core/SynthPresets.cpp src/core/Slicer.cpp src/core/Templates.cpp src/core/FlWrapper.cpp src/core/FlEffects.cpp src/core/Effector.cpp src/core/VintagePhaser.cpp src/core/FlSamplers.cpp src/core/FlSynths.cpp src/core/FlSytrus.cpp src/core/FlGenerators.cpp src/core/PhysicalModels.cpp src/core/FlPatcher.cpp src/core/Inflate.cpp src/core/WavPack.cpp src/core/Scheduler.cpp src/core/Instruments.cpp src/core/RackRenderer.cpp src/core/FlpImport.cpp src/core/Effects.cpp"

mkdir -p build-local build-local/wavpack
# WavPack decoder (C)
if [[ -z "${CC:-}" ]]; then
    case "$CXX" in
        *zig*) CC="${CXX/c++/cc}" ;;
        *clang++*) CC="${CXX/clang++/clang}" ;;
        *g++*) CC="${CXX/g++/gcc}" ;;
        *) CC=cc ;;
    esac
fi
for f in third_party/wavpack/src/*.c; do
    obj="build-local/wavpack/$(basename "$f" .c).o"
    [[ -f "$obj" && "$obj" -nt "$f" ]] || $CC -c -O2 -w -DHAVE___BUILTIN_CLZ=1 -Ithird_party/wavpack/include -Ithird_party/wavpack/src "$f" -o "$obj"
done
WAVPACK="$(ls build-local/wavpack/*.o | tr '\n' ' ')"
$CXX $FLAGS $CORE $WAVPACK tests/tests.cpp -o build-local/lynx_tests 2> >(grep -v 'glibc version' >&2)
$CXX $FLAGS src/flp/Flp.cpp tools/flpdump.cpp -o build-local/flpdump 2> >(grep -v 'glibc version' >&2)

$CXX $FLAGS $CORE $WAVPACK tools/renderwav.cpp -o build-local/renderwav 2> >(grep -v 'glibc version' >&2)
$CXX $FLAGS $CORE $WAVPACK tools/comparewav.cpp -o build-local/comparewav 2> >(grep -v 'glibc version' >&2)

./build-local/lynx_tests "$@"
