cmake_minimum_required(VERSION 3.22)

# Runtime estatico no Windows: o plugin nao depende de instalar o "VC++ Redistributable".
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# A versao e definida so aqui; ela aparece na janela do plugin e no Reaper.
project(DrikSampler VERSION 0.4.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.15
    GIT_SHALLOW ON)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(DrikSampler
    COMPANY_NAME "Adrik"
    PLUGIN_MANUFACTURER_CODE Adrk
    PLUGIN_CODE DrSm
    FORMATS VST3 Standalone
    PRODUCT_NAME "DrikSampler"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Instrument Sampler)

target_sources(DrikSampler PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(DrikSampler PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    DRIK_VERSION="${PROJECT_VERSION}")

target_link_libraries(DrikSampler
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags)

# Teste do motor de audio sem interface:  cmake -B build -DDRIK_BUILD_TESTS=ON
option(DRIK_BUILD_TESTS "Compila o teste do motor de audio" OFF)
if(DRIK_BUILD_TESTS)
    juce_add_console_app(DrikEngineTest PRODUCT_NAME "DrikEngineTest")
    target_sources(DrikEngineTest PRIVATE
        tests/test_engine.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp)
    target_compile_definitions(DrikEngineTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
        DRIK_VERSION="${PROJECT_VERSION}")
    target_link_libraries(DrikEngineTest PRIVATE juce::juce_audio_utils juce::juce_dsp)
endif()
