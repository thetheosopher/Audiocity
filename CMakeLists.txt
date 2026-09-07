cmake_minimum_required(VERSION 3.22)

project(Audiocity VERSION 1.3.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(AUDIOCITY_SELF_CONTAINED_RUNTIME "Statically link MSVC runtime (/MT, /MTd) for self-contained binaries." OFF)

if (MSVC AND AUDIOCITY_SELF_CONTAINED_RUNTIME)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif ()

option(AUDIOCITY_FETCH_JUCE "Fetch JUCE automatically when third_party/JUCE is unavailable" ON)
option(AUDIOCITY_ENABLE_ASIO "Enable ASIO device backend for Standalone builds (requires Steinberg ASIO SDK headers)." OFF)
option(AUDIOCITY_ENABLE_SANITIZERS "Instrument test targets with AddressSanitizer and, where supported, UndefinedBehaviorSanitizer." OFF)
option(AUDIOCITY_ENABLE_COVERAGE "Instrument test targets for source coverage reporting." OFF)
option(AUDIOCITY_BUILD_FUZZERS "Build importer corpus/libFuzzer entry points." OFF)
set(AUDIOCITY_ASIO_SDK_DIR "" CACHE PATH "Path to Steinberg ASIO SDK directory that contains common/iasiodrv.h")

function(audiocity_apply_test_instrumentation target)
    get_target_property(audiocity_target_type ${target} TYPE)
    set(audiocity_can_link TRUE)
    if (audiocity_target_type STREQUAL "OBJECT_LIBRARY"
        OR audiocity_target_type STREQUAL "STATIC_LIBRARY")
        set(audiocity_can_link FALSE)
    endif ()

    if (AUDIOCITY_ENABLE_SANITIZERS)
        if (MSVC)
            target_compile_options(${target} PRIVATE /fsanitize=address /Zi)
            if (audiocity_can_link)
                target_link_options(${target} PRIVATE /INCREMENTAL:NO)
            endif ()
        elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
            target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
            if (audiocity_can_link)
                target_link_options(${target} PRIVATE -fsanitize=address,undefined)
            endif ()
        else ()
            message(FATAL_ERROR "AUDIOCITY_ENABLE_SANITIZERS is unsupported by ${CMAKE_CXX_COMPILER_ID}")
        endif ()
    endif ()

    if (AUDIOCITY_ENABLE_COVERAGE)
        if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            target_compile_options(${target} PRIVATE -fprofile-instr-generate -fcoverage-mapping)
            if (audiocity_can_link)
                target_link_options(${target} PRIVATE -fprofile-instr-generate -fcoverage-mapping)
            endif ()
        elseif (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            target_compile_options(${target} PRIVATE --coverage)
            if (audiocity_can_link)
                target_link_options(${target} PRIVATE --coverage)
            endif ()
        elseif (NOT MSVC)
            message(FATAL_ERROR "AUDIOCITY_ENABLE_COVERAGE is unsupported by ${CMAKE_CXX_COMPILER_ID}")
        endif ()
    endif ()
endfunction()

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
        COMPANY_NAME "Michael A. McCloskey"
        COMPANY_COPYRIGHT "Copyright (c) 2026 Michael A. McCloskey"
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
        src/plugin/PluginAboutPage.cpp
        src/plugin/PluginCapturePage.cpp
        src/plugin/PluginGeneratePage.cpp
        src/plugin/PluginLibraryPage.cpp
        src/plugin/PluginMappingPage.cpp
        src/plugin/PluginPlayerPage.cpp
        src/plugin/PluginSampleHeaderPage.cpp
        src/plugin/PluginSampleInfoMetricsLayout.cpp
        src/plugin/PluginSampleInspectorCardLayout.cpp
        src/plugin/PluginEditor.cpp
        src/plugin/PluginInstrumentWorkspace.cpp
        src/plugin/PeakPreviewCache.cpp
        src/plugin/AudioStateCodec.cpp
        src/plugin/ImportedAssetResolver.cpp
        src/plugin/ImportedProgramState.cpp
        src/plugin/ImportedProgramStore.cpp
        src/plugin/PresetJson.cpp
        src/plugin/ProgramMappingModel.cpp
        src/plugin/PlayerPadState.cpp
        src/plugin/SampleBrowserTooltip.cpp
        src/plugin/CcLearnDial.cpp
        src/plugin/LibraryFileIndex.cpp
        src/plugin/ImportFormatRegistry.cpp
        src/plugin/LibraryMetadata.cpp
        src/engine/AudioFileSupport.cpp
        src/engine/EngineCore.cpp
        src/engine/LegacyNkiProbe.cpp
        src/engine/RexLoader.cpp
        src/engine/SfzImporter.cpp
        src/engine/SfzExporter.cpp
        src/engine/Sf2Importer.cpp
        src/engine/DecentSamplerImporter.cpp
        src/engine/DecentSamplerExporter.cpp
        src/engine/BitwigMultisampleImporter.cpp
        src/engine/XmlMultisampleImporterUtils.cpp
        src/engine/XmlMultisampleImporters.cpp
        src/engine/BinaryMultisampleImporters.cpp
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

# Copy the stock factory preset bank next to the freshly-built binaries so
# developers can browse them without running the installer first.
set(AUDIOCITY_FACTORY_PRESETS_DIR "${CMAKE_SOURCE_DIR}/assets/factory_presets")
if (EXISTS "${AUDIOCITY_FACTORY_PRESETS_DIR}")
    if (TARGET Audiocity_Standalone)
        add_custom_command(TARGET Audiocity_Standalone POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E rm -rf
                    "$<TARGET_FILE_DIR:Audiocity_Standalone>/FactoryPresets"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${AUDIOCITY_FACTORY_PRESETS_DIR}"
                "$<TARGET_FILE_DIR:Audiocity_Standalone>/FactoryPresets")
    endif ()
    if (TARGET Audiocity_VST3)
        add_custom_command(TARGET Audiocity_VST3 POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E rm -rf
                    "$<TARGET_FILE_DIR:Audiocity_VST3>/../Resources/FactoryPresets"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${AUDIOCITY_FACTORY_PRESETS_DIR}"
                "$<TARGET_FILE_DIR:Audiocity_VST3>/../Resources/FactoryPresets")
    endif ()
endif ()

include(CTest)
if (BUILD_TESTING)
    add_subdirectory(tests)
endif ()

# ---- Stock preset authoring tool -------------------------------------------
# Standalone offline executable that synthesizes embedded-sample factory
# presets and writes them to the assets/factory_presets directory.
add_executable(audiocity_preset_author
    tools/PresetAuthor.cpp
    src/plugin/PresetJson.cpp
    src/engine/Sf2Importer.cpp)

target_include_directories(audiocity_preset_author PRIVATE src)

target_compile_definitions(audiocity_preset_author
    PRIVATE
        AUDIOCITY_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)

target_link_libraries(audiocity_preset_author
    PRIVATE
        juce::juce_core
        juce::juce_data_structures
        juce::juce_audio_basics
        juce::juce_audio_formats
        juce::juce_recommended_config_flags)

if (MSVC)
    target_compile_options(audiocity_preset_author PRIVATE /W4 /permissive-)
endif ()

# ---- Preset auditioner -----------------------------------------------------
# Offline executable that loads every authored .acp, instantiates an
# EngineCore, renders a family-specific MIDI audition to a stereo WAV,
# and writes an objective metrics CSV plus a regressions report. Provides
# the "listening" feedback loop used to iterate on the factory bank.
add_executable(audiocity_preset_auditioner
    tools/PresetAuditioner.cpp
    src/plugin/PresetJson.cpp
    src/engine/EngineCore.cpp
    src/engine/AudioFileSupport.cpp
    src/engine/RexLoader.cpp
    src/engine/VoicePool.cpp)

target_include_directories(audiocity_preset_auditioner
    PRIVATE
        src
        third_party/REXSDK_Win_1.9.2)

target_compile_definitions(audiocity_preset_auditioner
    PRIVATE
        AUDIOCITY_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        REX_WINDOWS=1
        REX_MAC=0)

target_link_libraries(audiocity_preset_auditioner
    PRIVATE
        audiocity_rex_sdk
        juce::juce_core
        juce::juce_data_structures
        juce::juce_audio_basics
        juce::juce_audio_formats
        juce::juce_dsp
        juce::juce_events
        juce::juce_recommended_config_flags)

if (MSVC)
    target_compile_options(audiocity_preset_auditioner PRIVATE /W4 /permissive-)
endif ()
