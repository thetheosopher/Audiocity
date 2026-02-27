cmake_minimum_required(VERSION 3.22)

project(Audiocity VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(AUDIOCITY_FETCH_JUCE "Fetch JUCE automatically when third_party/JUCE is unavailable" ON)

if (EXISTS "${CMAKE_SOURCE_DIR}/third_party/JUCE/CMakeLists.txt")
    add_subdirectory(third_party/JUCE)
elseif (AUDIOCITY_FETCH_JUCE)
    include(FetchContent)
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
else ()
    message(FATAL_ERROR "JUCE not found. Add JUCE at third_party/JUCE or enable AUDIOCITY_FETCH_JUCE.")
endif ()

juce_add_plugin(Audiocity
    COMPANY_NAME "Audiocity"
    BUNDLE_ID "com.audiocity.plugin"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE AuCt
    PLUGIN_CODE AuCy
    FORMATS VST3 Standalone
    PRODUCT_NAME "Audiocity")

juce_generate_juce_header(Audiocity)

target_sources(Audiocity
    PRIVATE
        src/plugin/PluginProcessor.cpp
        src/plugin/PluginEditor.cpp
        src/engine/EngineCore.cpp
        src/engine/VoicePool.cpp
        src/engine/ZoneMap.cpp)

target_include_directories(Audiocity
    PRIVATE
        src)

target_compile_definitions(Audiocity
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)

target_link_libraries(Audiocity
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if (MSVC)
    target_compile_options(Audiocity PRIVATE /W4 /permissive-)
endif ()

include(CTest)
if (BUILD_TESTING)
    add_subdirectory(tests)
endif ()
