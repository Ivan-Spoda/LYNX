cmake_minimum_required(VERSION 3.22)

project(LYNX VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Núcleo sem JUCE: modelo, sequenciador, instrumentos internos e leitor de .flp

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
    src/core/WorkerPool.cpp)
target_include_directories(lynx_core PUBLIC src)

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

option(LYNX_BUILD_APP "Compila o app com JUCE" ON)

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
        COMPANY_NAME "Kauan")

    target_sources(LYNX PRIVATE
        src/Main.cpp
        src/engine/AudioEngine.cpp
        src/engine/ChannelRackProcessor.cpp
        src/engine/Metronome.cpp
        src/engine/SampleLibrary.cpp
        src/engine/ProjectFile.cpp
        src/engine/PluginHost.cpp
        src/engine/Exporter.cpp
        src/ui/MainComponent.cpp
        src/ui/ChannelRackComponent.cpp
        src/ui/PianoRollComponent.cpp
        src/ui/PlaylistComponent.cpp
        src/ui/MixerComponent.cpp
        src/ui/PluginWindow.cpp
        src/ui/ExportDialog.cpp
        src/ui/AutomationUi.cpp)

    target_compile_definitions(LYNX PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_PLUGINHOST_VST3=1
        JUCE_ASIO=0                      # ligar quando o ASIO SDK estiver disponível
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
