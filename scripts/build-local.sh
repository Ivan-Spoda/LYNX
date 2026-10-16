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
CORE="src/flp/Flp.cpp src/core/Song.cpp src/core/NoteEdit.cpp src/core/Recorder.cpp src/core/Pdc.cpp src/core/WorkerPool.cpp src/core/TimeStretch.cpp src/core/Arpeggiator.cpp src/core/SynthPresets.cpp src/core/Slicer.cpp src/core/Templates.cpp src/core/FlWrapper.cpp src/core/FlEffects.cpp src/core/Effector.cpp src/core/VintagePhaser.cpp src/core/Reeverb2.cpp src/core/LuxeVerb.cpp src/core/IldspMath.cpp src/core/ConvolverFx.cpp src/core/EquoFx.cpp src/core/PitchShifterFl.cpp src/core/SimSynth.cpp src/core/Wasp.cpp src/core/Granulizer.cpp src/core/Drumaxx.cpp src/core/FruityFilters.cpp src/core/NewtoneFx.cpp src/core/LovePhilter.cpp src/core/DistructorFx.cpp src/core/MultibandDelayFx.cpp src/core/FrequencySplitter.cpp src/core/Slayer.cpp src/core/FlangusFastDist.cpp src/core/FruityDelayBank.cpp src/core/FruityMultiband.cpp src/core/Vocodex.cpp src/core/FruityVocoder.cpp src/core/DelphiMath.cpp src/core/MaximusFx.cpp src/core/ParametricEqFl.cpp src/core/ParametricEqLinear.cpp src/core/FruityBasicFx.cpp src/core/TransistorBass.cpp src/core/GrossBeat.cpp src/core/StereoFx.cpp src/core/FruityDelay.cpp src/core/FruityShapers.cpp src/core/ChorusFx.cpp src/core/FruityDynamics.cpp src/core/FlSamplers.cpp src/core/FlSynths.cpp src/core/FlSytrus.cpp src/core/FlGenerators.cpp src/core/PhysicalModels.cpp src/core/DrumSynthLive.cpp src/core/SytrusEngine.cpp src/core/Harmless.cpp src/core/HarmlessFx.cpp src/core/Harmor.cpp src/core/Gms.cpp src/core/Morphine.cpp src/core/Ogun.cpp src/core/Toxic.cpp src/core/ToxicFx.cpp src/core/FruitKick.cpp src/core/BassDrum.cpp src/core/MiniSynth.cpp src/core/Dx10.cpp src/core/Kepler.cpp src/core/PoiZone.cpp src/core/FlPatcher.cpp src/core/Inflate.cpp src/core/WavPack.cpp src/core/Scheduler.cpp src/core/Instruments.cpp src/core/RackRenderer.cpp src/core/FlpImport.cpp src/core/Effects.cpp"

mkdir -p build-local build-local/wavpack
# workflow shell (key map, workspace layout, rack operations)
CORE="$CORE src/core/KeyMap.cpp src/core/Workspace.cpp src/core/RackOps.cpp"
# workflow P2 (note tools, mixer operations, history labels, browser search)
CORE="$CORE src/core/NoteTools.cpp src/core/MixerOps.cpp src/core/History.cpp src/core/SearchQuery.cpp"
# start-up sound (procedural)
CORE="$CORE src/core/StartupSound.cpp"
# FL's 3x Osc engine (REA round 47)
CORE="$CORE src/core/ThreeOsc.cpp"
# FL Studio integration: plugin database and sample relocation (read-only)
CORE="$CORE src/core/FlPluginDb.cpp"
# missing files of an opened project: search and relocation (round 81)
CORE="$CORE src/core/MissingFiles.cpp"
# spectral waveforms (band analysis, multi-resolution peak cache)
CORE="$CORE src/core/WaveAnalysis.cpp"
# FL's Plucked!, Sawer and BeepMap engines (REA round 65)
CORE="$CORE src/core/Plucked.cpp src/core/BeepMap.cpp src/core/Sawer.cpp"
# FL's sampler interpolation and FL Keys (REA round 69)
CORE="$CORE src/core/FlResampler.cpp src/core/FlKeys.cpp"
# FL's Hardcore (REA round 71)
CORE="$CORE src/core/HardcoreFx.cpp"
# FL's DirectWave (REA round 72)
CORE="$CORE src/core/DirectWave.cpp"
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
$CXX $FLAGS $CORE $WAVPACK tools/vocodexrender.cpp -o build-local/vocodexrender 2> >(grep -v 'glibc version' >&2)
$CXX $FLAGS src/core/Harmor.cpp src/core/HarmlessFx.cpp tools/harmorrender.cpp -o build-local/harmorrender 2> >(grep -v 'glibc version' >&2)
$CXX $FLAGS src/core/Gms.cpp tools/gmsrender.cpp -o build-local/gmsrender 2> >(grep -v 'glibc version' >&2)
$CXX $FLAGS src/core/Morphine.cpp tools/morphinerender.cpp -o build-local/morphinerender 2> >(grep -v 'glibc version' >&2)
$CXX $FLAGS src/core/FruitKick.cpp src/core/FlangusFastDist.cpp tools/fruitkickrender.cpp -o build-local/fruitkickrender 2> >(grep -v 'glibc version' >&2)
$CXX $FLAGS src/core/BassDrum.cpp tools/bassdrumrender.cpp -o build-local/bassdrumrender 2> >(grep -v 'glibc version' >&2)
$CXX $FLAGS src/core/Toxic.cpp src/core/ToxicFx.cpp tools/toxicrender.cpp -o build-local/toxicrender 2> >(grep -v 'glibc version' >&2)
$CXX $FLAGS $CORE $WAVPACK tools/ogunrender.cpp -o build-local/ogunrender 2> >(grep -v 'glibc version' >&2)
$CXX $FLAGS src/core/MiniSynth.cpp tools/minisynthrender.cpp -o build-local/minisynthrender 2> >(grep -v 'glibc version' >&2)
$CXX $FLAGS src/core/SimSynth.cpp tools/simsynthrender.cpp -o build-local/simsynthrender 2> >(grep -v 'glibc version' >&2)
$CXX $FLAGS src/core/Dx10.cpp tools/dx10render.cpp -o build-local/dx10render 2> >(grep -v 'glibc version' >&2)
$CXX $FLAGS src/core/Kepler.cpp tools/keplerrender.cpp -o build-local/keplerrender 2> >(grep -v 'glibc version' >&2)
$CXX $FLAGS src/core/TransistorBass.cpp tools/tbassrender.cpp -o build-local/tbassrender 2> >(grep -v 'glibc version' >&2)
$CXX $FLAGS src/core/PoiZone.cpp tools/poizonerender.cpp -o build-local/poizonerender 2> >(grep -v 'glibc version' >&2)
$CXX $FLAGS src/core/FruityDelay.cpp tools/fdelayrender.cpp -o build-local/fdelayrender 2> >(grep -v 'glibc version' >&2)
$CXX $FLAGS $CORE $WAVPACK tools/distructorrender.cpp -o build-local/distructorrender 2> >(grep -v 'glibc version' >&2)
$CXX $FLAGS src/core/ThreeOsc.cpp tools/threeoscrender.cpp -o build-local/threeoscrender 2> >(grep -v 'glibc version' >&2)
$CXX $FLAGS src/core/Slayer.cpp tools/slayerrender.cpp -o build-local/slayerrender 2> >(grep -v 'glibc version' >&2)

./build-local/lynx_tests "$@"
