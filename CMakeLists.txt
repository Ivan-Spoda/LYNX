cmake_minimum_required(VERSION 3.22)

project(Daw VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Núcleo sem JUCE: modelo, sequenciador, instrumentos internos e leitor de .flp

add_library(daw_core STATIC
    src/flp/Flp.cpp
    src/core/Song.cpp
    src/core/Scheduler.cpp
    src/core/Instruments.cpp
    src/core/RackRenderer.cpp
    src/core/FlpImport.cpp)
target_include_directories(daw_core PUBLIC src)

add_executable(daw_tests tests/tests.cpp)
target_link_libraries(daw_tests PRIVATE daw_core)

add_executable(flpdump tools/flpdump.cpp)
target_link_libraries(flpdump PRIVATE daw_core)

add_executable(renderwav tools/renderwav.cpp)
target_link_libraries(renderwav PRIVATE daw_core)

enable_testing()
add_test(NAME daw_tests COMMAND daw_tests)

# ---------------------------------------------------------------------------
# App (JUCE)

option(DAW_BUILD_APP "Compila o app com JUCE" ON)

if(DAW_BUILD_APP)
    include(FetchContent)
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW ON)
    FetchContent_MakeAvailable(JUCE)

    juce_add_gui_app(Daw
        PRODUCT_NAME "Daw"
        COMPANY_NAME "Kauan")

    target_sources(Daw PRIVATE
        src/Main.cpp
        src/engine/AudioEngine.cpp
        src/engine/ChannelRackProcessor.cpp
        src/engine/Metronome.cpp
        src/engine/SampleLibrary.cpp
        src/engine/ProjectFile.cpp
        src/ui/MainComponent.cpp
        src/ui/ChannelRackComponent.cpp
        src/ui/PianoRollComponent.cpp)

    target_compile_definitions(Daw PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_PLUGINHOST_VST3=1
        JUCE_ASIO=0                      # ligar quando o ASIO SDK estiver disponível
        JUCE_USE_MP3AUDIOFORMAT=1
        JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:Daw,JUCE_PRODUCT_NAME>"
        JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:Daw,JUCE_VERSION>")

    target_link_libraries(Daw
        PRIVATE
            daw_core
            juce::juce_audio_utils
            juce::juce_audio_processors
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
endif()
