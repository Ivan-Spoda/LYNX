cmake_minimum_required(VERSION 3.22)

project(LYNX VERSION 0.1.5 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# JUCE-free core: model, sequencer, built-in instruments and .flp reader

add_library(lynx_core STATIC
    src/flp/Flp.cpp
    src/core/Song.cpp
    src/core/Scheduler.cpp
    src/core/Instruments.cpp
    src/core/RackRenderer.cpp
    src/core/FlpImport.cpp
    src/core/Effects.cpp
    src/core/NoteEdit.cpp
    src/core/Recorder.cpp
    src/core/Pdc.cpp
    src/core/WorkerPool.cpp
    src/core/TimeStretch.cpp
    src/core/Arpeggiator.cpp
    src/core/SynthPresets.cpp
    src/core/Slicer.cpp
    src/core/Templates.cpp
    src/core/FlWrapper.cpp
    src/core/FlPluginDb.cpp
    src/core/MissingFiles.cpp
    src/core/Preferences.cpp
    src/core/FlEffects.cpp
    src/core/Effector.cpp
    src/core/FlSamplers.cpp
    src/core/FlSynths.cpp
    src/core/FlSytrus.cpp
    src/core/FlGenerators.cpp
    src/core/PhysicalModels.cpp
    src/core/DrumSynthLive.cpp
    src/core/Harmless.cpp
    src/core/Harmor.cpp
    src/core/Gms.cpp
    src/core/Morphine.cpp
    src/core/FruitKick.cpp
    src/core/BassDrum.cpp
    src/core/Ogun.cpp
    src/core/MiniSynth.cpp
    src/core/SimSynth.cpp
    src/core/Wasp.cpp
    src/core/Granulizer.cpp
    src/core/Drumaxx.cpp
    src/core/Slayer.cpp
    src/core/Dx10.cpp
    src/core/Kepler.cpp
    src/core/TransistorBass.cpp
    src/core/PoiZone.cpp
    src/core/Toxic.cpp
    src/core/ToxicFx.cpp
    src/core/HarmlessFx.cpp
    src/core/SytrusEngine.cpp
    src/core/VintagePhaser.cpp
    src/core/Reeverb2.cpp
    src/core/LuxeVerb.cpp
    src/core/IldspMath.cpp
    src/core/ConvolverFx.cpp
    src/core/Vocodex.cpp
    src/core/MaximusFx.cpp
    src/core/GrossBeat.cpp
    src/core/LovePhilter.cpp
    src/core/FruityDynamics.cpp
    src/core/ParametricEqFl.cpp
    src/core/ParametricEqLinear.cpp
    src/core/EquoFx.cpp
    src/core/MultibandDelayFx.cpp
    src/core/PitchShifterFl.cpp
    src/core/FruityShapers.cpp
    src/core/FruityDelay.cpp
    src/core/StereoFx.cpp
    src/core/FrequencySplitter.cpp
    src/core/DistructorFx.cpp
    src/core/ChorusFx.cpp
    src/core/FruityBasicFx.cpp
    src/core/FruityDelayBank.cpp
    src/core/FruityVocoder.cpp
    src/core/DelphiMath.cpp
    src/core/FruityMultiband.cpp
    src/core/FlangusFastDist.cpp
    src/core/FruityFilters.cpp
    src/core/NewtoneFx.cpp
    src/core/FlPatcher.cpp
    src/core/ThreeOsc.cpp
    src/core/Plucked.cpp
    src/core/HardcoreFx.cpp
    src/core/FlResampler.cpp
    src/core/FlKeys.cpp
    src/core/DirectWave.cpp
    src/core/OggVorbis.cpp
    src/core/BeepMap.cpp
    src/core/Sawer.cpp
    src/core/Inflate.cpp
    src/core/WavPack.cpp)
target_include_directories(lynx_core PUBLIC src)

# workflow shell (key map, workspace layout, rack operations)
target_sources(lynx_core PRIVATE
    src/core/KeyMap.cpp
    src/core/Workspace.cpp
    src/core/RackOps.cpp
    src/core/StartupSound.cpp)

# workflow P2 (note tools, mixer operations, history labels, browser search)
target_sources(lynx_core PRIVATE
    src/core/NoteTools.cpp
    src/core/PianoRollTools.cpp
    src/core/EventEdit.cpp
    src/core/MixerOps.cpp
    src/core/History.cpp
    src/core/SearchQuery.cpp)

# spectral waveforms: band analysis and multi-resolution peak cache
target_sources(lynx_core PRIVATE
    src/core/WaveAnalysis.cpp)

# export options + MP3, plugin performance monitor, clip properties / arrangements (round 84)
target_sources(lynx_core PRIVATE
    src/core/ExportOptions.cpp
    src/core/Mp3Encoder.cpp
    src/core/PluginMonitor.cpp
    src/core/PlaylistOps.cpp)

# LAME's libmp3lame (LGPL; third_party/lame, encoder only, configured by third_party/lame/src/config.h)
file(GLOB LAME_SOURCES third_party/lame/src/*.c)
add_library(lame_encoder STATIC ${LAME_SOURCES})
target_include_directories(lame_encoder PUBLIC third_party/lame/include PRIVATE third_party/lame/src)
target_compile_definitions(lame_encoder PRIVATE HAVE_CONFIG_H)
target_compile_options(lame_encoder PRIVATE $<$<NOT:$<C_COMPILER_ID:MSVC>>:-w>)
target_link_libraries(lynx_core PUBLIC lame_encoder)

# WavPack decoder (BSD; read-only) for the .wv samples in the FL packs
file(GLOB WAVPACK_SOURCES third_party/wavpack/src/*.c)
add_library(wavpack_decoder STATIC ${WAVPACK_SOURCES})
target_include_directories(wavpack_decoder PUBLIC third_party/wavpack/include PRIVATE third_party/wavpack/src)
target_compile_definitions(wavpack_decoder PRIVATE $<$<NOT:$<C_COMPILER_ID:MSVC>>:HAVE___BUILTIN_CLZ=1>)
target_link_libraries(lynx_core PUBLIC wavpack_decoder)

# libogg + libvorbis decoder (BSD; third_party/vorbis, mdct.c modified as FL builds it, REA_rodada77) for FL's
# Ogg-in-WAV samples; no FMA contraction so the float operation order stays libvorbis'
file(GLOB VORBIS_SOURCES third_party/vorbis/src/*.c)
list(FILTER VORBIS_SOURCES EXCLUDE REGEX "/lookup\\.c$") # (included by lsp.c)
add_library(vorbis_decoder STATIC ${VORBIS_SOURCES})
target_include_directories(vorbis_decoder PUBLIC third_party/vorbis/include PRIVATE third_party/vorbis/src)
target_compile_options(vorbis_decoder PRIVATE $<$<NOT:$<C_COMPILER_ID:MSVC>>:-ffp-contract=off -w>
    # every symbol gets a lynx_ prefix (JUCE links its own copy with the same C names)
    $<IF:$<C_COMPILER_ID:MSVC>,/FI${CMAKE_CURRENT_SOURCE_DIR}/third_party/vorbis/include/lynx_vorbis_prefix.h,-include${CMAKE_CURRENT_SOURCE_DIR}/third_party/vorbis/include/lynx_vorbis_prefix.h>)
target_link_libraries(lynx_core PUBLIC vorbis_decoder)

add_executable(lynx_tests tests/tests.cpp)
target_link_libraries(lynx_tests PRIVATE lynx_core)

add_executable(flpdump tools/flpdump.cpp)
target_link_libraries(flpdump PRIVATE lynx_core)

add_executable(renderwav tools/renderwav.cpp)
target_link_libraries(renderwav PRIVATE lynx_core)

enable_testing()
add_test(NAME lynx_tests COMMAND lynx_tests)

# ---------------------------------------------------------------------------
# App (JUCE)

option(LYNX_BUILD_APP "Build the app with JUCE" ON)

if(LYNX_BUILD_APP)
    include(FetchContent)
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW ON)
    FetchContent_MakeAvailable(JUCE)

    juce_add_gui_app(LYNX
        PRODUCT_NAME "LYNX"
        COMPANY_NAME "Ka1-S"
        COMPANY_COPYRIGHT "Copyright (C) 2026 Ka1-S. Licensed under the GNU AGPL-3.0."
        DESCRIPTION "LYNX digital audio workstation")

    target_sources(LYNX PRIVATE
        src/Main.cpp
        src/platform/FileTypes.cpp
        src/engine/AudioEngine.cpp
        src/engine/ChannelRackProcessor.cpp
        src/engine/Metronome.cpp
        src/engine/SampleLibrary.cpp
    src/engine/WavPackFormat.cpp
        src/engine/ProjectFile.cpp
        src/engine/PluginHost.cpp
        src/engine/FlStudio.cpp
        src/engine/PluginSandbox.cpp
        src/engine/Exporter.cpp
        src/engine/StreamPrefetcher.cpp
        src/engine/ClapFormat.cpp
        src/engine/Vst2Format.cpp
        src/engine/MidiFileIo.cpp
        src/engine/PreviewPlayer.cpp
        src/ui/MainComponent.cpp
        src/ui/ChannelRackComponent.cpp
        src/ui/PianoRollComponent.cpp
        src/ui/PlaylistComponent.cpp
        src/ui/MixerComponent.cpp
        src/ui/PluginWindow.cpp
        src/ui/WindowOwner.cpp
        src/ui/ParametricEqEditor.cpp
        src/ui/ExportDialog.cpp
        src/ui/AutomationUi.cpp
        src/ui/ChannelSettings.cpp
        src/ui/BrowserComponent.cpp
        src/ui/Theme.cpp
        src/ui/Icons.cpp
        src/ui/I18n.cpp
        src/ui/EditorTools.cpp)

    # workflow shell: workspace desktop, control contract, dialogs
    target_sources(LYNX PRIVATE
        src/ui/Workspace.cpp
        src/ui/ValueControl.cpp
        src/ui/Dialogs.cpp
        src/ui/SetupWizard.cpp
        src/ui/Splash.cpp)

    # main window chrome: hint bar and toolbar widgets
    target_sources(LYNX PRIVATE
        src/ui/HintBar.cpp
        src/ui/Toolbar.cpp)

    # spectral waveforms: background analysis cache and drawing
    target_sources(LYNX PRIVATE
        src/ui/WaveformView.cpp)

    # workflow P2: note tool dialogs, tool windows, undo history, quick-add palette
    target_sources(LYNX PRIVATE
        src/ui/NoteToolDialogs.cpp
        src/ui/EventEditor.cpp
        src/ui/ToolWindow.cpp
        src/ui/HistoryPanel.cpp
        src/ui/MissingFilesDialog.cpp
        src/ui/SettingsDialog.cpp
        src/ui/SettingsPages.cpp
        src/ui/PluginManager.cpp
        src/ui/ShortcutManager.cpp
        src/ui/NewProjectDialog.cpp
        src/ui/ClipProperties.cpp
        src/ui/PluginMonitorWindow.cpp
        src/ui/TouchKeyboard.cpp
        src/ui/QuickAddPalette.cpp)

    target_include_directories(LYNX PRIVATE third_party/asiosdk/common)

    target_compile_definitions(LYNX PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_PLUGINHOST_VST3=1
        JUCE_ASIO=1                      # ASIO SDK headers in third_party/asiosdk (GPLv3 option)
        JUCE_USE_MP3AUDIOFORMAT=1
        JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:LYNX,JUCE_PRODUCT_NAME>"
        JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:LYNX,JUCE_VERSION>")

    target_link_libraries(LYNX
        PRIVATE
            lynx_core
            juce::juce_audio_utils
            juce::juce_audio_processors
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
endif()
