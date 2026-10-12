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
CORE="src/flp/Flp.cpp src/core/Song.cpp src/core/Scheduler.cpp src/core/Instruments.cpp src/core/RackRenderer.cpp src/core/FlpImport.cpp"

mkdir -p build-local
$CXX $FLAGS $CORE tests/tests.cpp -o build-local/daw_tests 2> >(grep -v 'glibc version' >&2)
$CXX $FLAGS src/flp/Flp.cpp tools/flpdump.cpp -o build-local/flpdump 2> >(grep -v 'glibc version' >&2)

$CXX $FLAGS $CORE tools/renderwav.cpp -o build-local/renderwav 2> >(grep -v 'glibc version' >&2)

./build-local/daw_tests "$@"
