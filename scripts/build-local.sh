#!/usr/bin/env bash
# Compila e testa o núcleo (sem JUCE) com qualquer compilador C++20.
# Usa $CXX se definido; senão tenta o Zig em ~/opt, depois g++/clang++.
set -euo pipefail
cd "$(dirname "$0")/.."

if [[ -z "${CXX:-}" ]]; then
    if [[ -x "$HOME/opt/zig-linux-x86_64-0.13.0/zig" ]]; then
        CXX="$HOME/opt/zig-linux-x86_64-0.13.0/zig c++"
    else
        CXX="$(command -v clang++ || command -v g++)"
    fi
fi

FLAGS="-std=c++20 -O2 -g -Wall -Wextra -Wpedantic -Isrc"
CORE="src/flp/Flp.cpp src/core/Song.cpp src/core/NoteEdit.cpp src/core/Recorder.cpp src/core/Pdc.cpp src/core/WorkerPool.cpp src/core/TimeStretch.cpp src/core/Arpeggiator.cpp src/core/SynthPresets.cpp src/core/Slicer.cpp src/core/Templates.cpp src/core/FlWrapper.cpp src/core/FlEffects.cpp src/core/FlSamplers.cpp src/core/FlSynths.cpp src/core/Scheduler.cpp src/core/Instruments.cpp src/core/RackRenderer.cpp src/core/FlpImport.cpp src/core/Effects.cpp"

mkdir -p build-local
$CXX $FLAGS $CORE tests/tests.cpp -o build-local/lynx_tests 2> >(grep -v 'glibc version' >&2)
$CXX $FLAGS src/flp/Flp.cpp tools/flpdump.cpp -o build-local/flpdump 2> >(grep -v 'glibc version' >&2)

$CXX $FLAGS $CORE tools/renderwav.cpp -o build-local/renderwav 2> >(grep -v 'glibc version' >&2)
$CXX $FLAGS $CORE tools/comparewav.cpp -o build-local/comparewav 2> >(grep -v 'glibc version' >&2)

./build-local/lynx_tests "$@"
