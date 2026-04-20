cmake_minimum_required(VERSION 3.22)

project(Audiocity VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(AUDIOCITY_SELF_CONTAINED_RUNTIME "Statically link MSVC runtime (/MT, /MTd) for self-contained binaries." OFF)

if (MSVC AND AUDIOCITY_SELF_CONTAINED_RUNTIME)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif ()

option(AUDIOCITY_FETCH_JUCE "Fetch JUCE automatically when third_party/JUCE is unavailable" ON)
option(AUDIOCITY_ENABLE_ASIO "Enable ASIO device backend for Standalone builds (requires Steinberg ASIO SDK headers)." OFF)
set(AUDIOCITY_ASIO_SDK_DIR "" CACHE PATH "Path to Steinberg ASIO SDK directory that contains common/iasiodrv.h")

set(AUDIOCITY_JUCE_ASIO 0)
set(AUDIOCITY_ASIO_HEADER_DIR "")
set(AUDIOCITY_ASIO_HOST_DIR "")

if (WIN32 AND AUDIOCITY_ENABLE_ASIO)
    if (AUDIOCITY_ASIO_SDK_DIR STREQUAL "")
        set(AUDIOCITY_ASIO_CANDIDATE "${CMAKE_SOURCE_DIR}/third_party/asiosdk")
    else ()
        set(AUDIOCITY_ASIO_CANDIDATE "${AUDIOCITY_ASIO_SDK_DIR}")
    endif ()

    if (EXISTS "${AUDIOCITY_ASIO_CANDIDATE}/common/iasiodrv.h")
        set(AUDIOCITY_ASIO_HEADER_DIR "${AUDIOCITY_ASIO_CANDIDATE}/common")
        if (EXISTS "${AUDIOCITY_ASIO_CANDIDATE}/host")
            set(AUDIOCITY_ASIO_HOST_DIR "${AUDIOCITY_ASIO_CANDIDATE}/host")
        endif ()
    elseif (EXISTS "${AUDIOCITY_ASIO_CANDIDATE}/ASIOSDK/common/iasiodrv.h")
        set(AUDIOCITY_ASIO_HEADER_DIR "${AUDIOCITY_ASIO_CANDIDATE}/ASIOSDK/common")
        if (EXISTS "${AUDIOCITY_ASIO_CANDIDATE}/ASIOSDK/host")
            set(AUDIOCITY_ASIO_HOST_DIR "${AUDIOCITY_ASIO_CANDIDATE}/ASIOSDK/host")
        endif ()
    else ()
        message(FATAL_ERROR "AUDIOCITY_ENABLE_ASIO is ON but ASIO SDK headers were not found at '${AUDIOCITY_ASIO_CANDIDATE}'. Set AUDIOCITY_ASIO_SDK_DIR to a valid ASIO SDK path containing common/iasiodrv.h.")
    endif ()

    set(AUDIOCITY_JUCE_ASIO 1)
endif ()

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
    ICON_BIG "${CMAKE_SOURCE_DIR}/assets/icons/audiocity_icon_512.png"
    ICON_SMALL "${CMAKE_SOURCE_DIR}/assets/icons/audiocity_icon_128.png"
    PRODUCT_NAME "Audiocity")

juce_generate_juce_header(Audiocity)

juce_add_binary_data(AudiocityBinaryData SOURCES
    assets/icons/audiocity_icon_128.png)

add_library(audiocity_rex_sdk STATIC
    third_party/REXSDK_Win_1.9.2/REX.c)

target_include_directories(audiocity_rex_sdk
    PUBLIC
        third_party/REXSDK_Win_1.9.2)

target_compile_definitions(audiocity_rex_sdk
    PRIVATE
        REX_WINDOWS=1
        REX_MAC=0)

if (MSVC)
    target_compile_options(audiocity_rex_sdk PRIVATE /W0 /WX-)
    target_link_libraries(audiocity_rex_sdk PRIVATE Version)
else ()
    target_compile_options(audiocity_rex_sdk PRIVATE -w)
endif ()

target_sources(Audiocity
    PRIVATE
        src/plugin/PluginProcessor.cpp
        src/plugin/PluginEditor.cpp
        src/plugin/PeakPreviewCache.cpp
        src/plugin/PresetJson.cpp
        src/plugin/PlayerPadState.cpp
        src/plugin/CcLearnDial.cpp
        src/engine/EngineCore.cpp
        src/engine/RexLoader.cpp
        src/engine/VoicePool.cpp)

target_include_directories(Audiocity
    PRIVATE
        src)

if (NOT AUDIOCITY_ASIO_HEADER_DIR STREQUAL "")
    target_include_directories(Audiocity PRIVATE "${AUDIOCITY_ASIO_HEADER_DIR}")
endif ()

if (NOT AUDIOCITY_ASIO_HOST_DIR STREQUAL "")
    target_include_directories(Audiocity PRIVATE "${AUDIOCITY_ASIO_HOST_DIR}")
endif ()

target_compile_definitions(Audiocity
    PRIVATE
        REX_WINDOWS=1
        REX_MAC=0
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_ASIO=${AUDIOCITY_JUCE_ASIO})

target_link_libraries(Audiocity
    PRIVATE
        AudiocityBinaryData
        audiocity_rex_sdk
        juce::juce_audio_utils
        juce::juce_data_structures
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if (MSVC)
    target_compile_options(Audiocity PRIVATE /W4 /WX /permissive-)
endif ()

set(AUDIOCITY_REX_DEPLOY_DLL
    "${CMAKE_SOURCE_DIR}/third_party/REXSDK_Win_1.9.2/Win/x64/Deployment/REX Shared Library.dll")

if (EXISTS "${AUDIOCITY_REX_DEPLOY_DLL}")
    foreach (rex_target Audiocity_Standalone Audiocity_VST3)
        if (TARGET ${rex_target})
            add_custom_command(TARGET ${rex_target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${AUDIOCITY_REX_DEPLOY_DLL}"
                    "$<TARGET_FILE_DIR:${rex_target}>/REX Shared Library.dll")
        endif ()
    endforeach ()
endif ()

include(CTest)
if (BUILD_TESTING)
    add_subdirectory(tests)
endif ()
