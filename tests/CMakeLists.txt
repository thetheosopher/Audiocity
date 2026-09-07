include("${CMAKE_CURRENT_LIST_DIR}/ImporterFuzzTargets.cmake")

set(AUDIOCITY_ENGINE_CORE_TEST_SOURCES
    ../src/engine/AudioFileSupport.cpp
    ../src/engine/EngineCore.cpp
    ../src/engine/RexLoader.cpp
    ../src/engine/VoicePool.cpp)

set(AUDIOCITY_EXTENDED_TEST_SUPPORT_SOURCES
    ../src/plugin/PeakPreviewCache.cpp
    ../src/plugin/AudioStateCodec.cpp
    ../src/plugin/ImportedAssetResolver.cpp
    ../src/plugin/ImportedProgramState.cpp
    ../src/plugin/ImportedProgramStore.cpp
    ../src/plugin/PresetJson.cpp
    ../src/plugin/ProgramMappingModel.cpp
    ../src/plugin/CcLearnDial.cpp
    ../src/plugin/PlayerPadState.cpp
    ../src/plugin/SampleBrowserTooltip.cpp
    ../src/plugin/LibraryFileIndex.cpp
    ../src/plugin/ImportFormatRegistry.cpp
    ../src/plugin/LibraryMetadata.cpp
    ../src/engine/LegacyNkiProbe.cpp
    ../src/engine/SfzImporter.cpp
    ../src/engine/SfzExporter.cpp
    ../src/engine/Sf2Importer.cpp
    ../src/engine/DecentSamplerImporter.cpp
    ../src/engine/DecentSamplerExporter.cpp
    ../src/engine/BitwigMultisampleImporter.cpp
    ../src/engine/XmlMultisampleImporterUtils.cpp
    ../src/engine/XmlMultisampleImporters.cpp
    ../src/engine/BinaryMultisampleImporters.cpp)

set(AUDIOCITY_TEST_JUCE_MODULES
    juce::juce_gui_basics
    juce::juce_audio_basics
    juce::juce_audio_formats
    juce::juce_data_structures
    juce::juce_dsp
    juce::juce_events
    juce::juce_core)

set(AUDIOCITY_UI_TEST_JUCE_MODULES
    ${AUDIOCITY_TEST_JUCE_MODULES}
    juce::juce_audio_devices
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_graphics
    juce::juce_gui_extra)

function(audiocity_use_juce_headers target)
    foreach (audiocity_juce_module IN LISTS ARGN)
        get_target_property(audiocity_juce_include_dirs ${audiocity_juce_module} INTERFACE_INCLUDE_DIRECTORIES)
        if (audiocity_juce_include_dirs)
            target_include_directories(${target} PRIVATE ${audiocity_juce_include_dirs})
        endif ()
        get_target_property(audiocity_juce_compile_definitions ${audiocity_juce_module} INTERFACE_COMPILE_DEFINITIONS)
        if (audiocity_juce_compile_definitions)
            target_compile_definitions(${target} PRIVATE ${audiocity_juce_compile_definitions})
        endif ()
    endforeach ()
endfunction()

add_library(audiocity_engine_test_support STATIC
    ${AUDIOCITY_ENGINE_CORE_TEST_SOURCES}
    ${AUDIOCITY_EXTENDED_TEST_SUPPORT_SOURCES})

function(audiocity_configure_test_code target link_juce_modules)
    target_include_directories(${target}
        PRIVATE
            ../src
            ../third_party/REXSDK_Win_1.9.2)

    target_compile_definitions(${target}
        PRIVATE
            AUDIOCITY_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
            REX_WINDOWS=1
            REX_MAC=0)

    audiocity_use_juce_headers(${target} ${AUDIOCITY_TEST_JUCE_MODULES})
    if (link_juce_modules)
        target_link_libraries(${target} PRIVATE ${AUDIOCITY_TEST_JUCE_MODULES})
    endif ()

    if (MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX /permissive-)
    endif ()
    audiocity_apply_test_instrumentation(${target})
endfunction()

audiocity_configure_test_code(audiocity_engine_test_support TRUE)

add_executable(audiocity_offline_tests OfflineRenderTests.cpp)
target_link_libraries(audiocity_offline_tests
    PRIVATE
        audiocity_engine_test_support
        audiocity_rex_sdk)
audiocity_configure_test_code(audiocity_offline_tests FALSE)

add_executable(audiocity_engine_profile EngineProfileHarness.cpp)
target_link_libraries(audiocity_engine_profile
    PRIVATE
        audiocity_engine_test_support
        audiocity_rex_sdk)
audiocity_configure_test_code(audiocity_engine_profile FALSE)

set(AUDIOCITY_OFFLINE_TEST_EXE "$<TARGET_FILE:audiocity_offline_tests>")

foreach (audiocity_test_suite IN ITEMS engine import state preset)
    add_test(
        NAME audiocity_offline_${audiocity_test_suite}
        COMMAND "${CMAKE_COMMAND}"
            -DAUDIOCITY_TEST_EXE=${AUDIOCITY_OFFLINE_TEST_EXE}
            -DAUDIOCITY_TEST_SUITE=${audiocity_test_suite}
            -P "${CMAKE_CURRENT_LIST_DIR}/RunOfflineTestExecutable.cmake")
    set_tests_properties(audiocity_offline_${audiocity_test_suite}
        PROPERTIES LABELS "offline;${audiocity_test_suite}")
endforeach ()

if (AUDIOCITY_BUILD_FUZZERS)
    set(audiocity_importer_fuzz_targets)
    foreach (audiocity_fuzz_format IN LISTS AUDIOCITY_IMPORTER_FUZZ_FORMATS)
        set(audiocity_fuzz_target "audiocity_fuzz_${audiocity_fuzz_format}")
        audiocity_prepare_importer_fuzz_corpus(
            "${audiocity_fuzz_format}"
            audiocity_fuzz_corpus_directory
            audiocity_fuzz_corpus_inputs)
        add_executable(${audiocity_fuzz_target} ImporterFuzzHarness.cpp)
        target_compile_definitions(${audiocity_fuzz_target}
            PRIVATE AUDIOCITY_FUZZ_FORMAT="${audiocity_fuzz_format}")
        target_link_libraries(${audiocity_fuzz_target}
            PRIVATE
                audiocity_engine_test_support
                audiocity_rex_sdk)
        audiocity_configure_test_code(${audiocity_fuzz_target} FALSE)

        if (CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND NOT MSVC)
            target_compile_definitions(${audiocity_fuzz_target} PRIVATE AUDIOCITY_LIBFUZZER=1)
            target_compile_options(${audiocity_fuzz_target} PRIVATE -fsanitize=fuzzer)
            target_link_options(${audiocity_fuzz_target} PRIVATE -fsanitize=fuzzer)
            add_test(NAME ${audiocity_fuzz_target}_corpus
                COMMAND "$<TARGET_FILE:${audiocity_fuzz_target}>"
                    "${audiocity_fuzz_corpus_directory}"
                    -runs=1000 -seed=424242 -max_len=65536 -timeout=10)
        else ()
            add_test(NAME ${audiocity_fuzz_target}_corpus
                COMMAND "$<TARGET_FILE:${audiocity_fuzz_target}>" ${audiocity_fuzz_corpus_inputs})
        endif ()
        set_tests_properties(${audiocity_fuzz_target}_corpus
            PROPERTIES LABELS "fuzz;import" TIMEOUT 45)
        list(APPEND audiocity_importer_fuzz_targets ${audiocity_fuzz_target})
    endforeach ()
    add_custom_target(audiocity_importer_fuzzers DEPENDS ${audiocity_importer_fuzz_targets})
endif ()

add_test(
    NAME audiocity_packaging_config
    COMMAND "${CMAKE_COMMAND}"
        -DAUDIOCITY_SOURCE_DIR=${CMAKE_SOURCE_DIR}
        -P "${CMAKE_CURRENT_LIST_DIR}/PackagingConfigTests.cmake")

add_executable(audiocity_ui_snapshot_smoke
    UiSnapshotSmoke.cpp)

add_executable(audiocity_ui_snapshot_header_smoke
    UiProcessorHeaderSmoke.cpp)

target_include_directories(audiocity_ui_snapshot_smoke
    PRIVATE
        ../third_party/REXSDK_Win_1.9.2
        ${CMAKE_BINARY_DIR}/Audiocity_artefacts/JuceLibraryCode)

target_include_directories(audiocity_ui_snapshot_header_smoke
    PRIVATE
        ../src
        ../third_party/REXSDK_Win_1.9.2
        ${CMAKE_BINARY_DIR}/Audiocity_artefacts/JuceLibraryCode)

if (NOT AUDIOCITY_ASIO_HEADER_DIR STREQUAL "")
    target_include_directories(audiocity_ui_snapshot_smoke PRIVATE "${AUDIOCITY_ASIO_HEADER_DIR}")
    target_include_directories(audiocity_ui_snapshot_header_smoke PRIVATE "${AUDIOCITY_ASIO_HEADER_DIR}")
endif ()

if (NOT AUDIOCITY_ASIO_HOST_DIR STREQUAL "")
    target_include_directories(audiocity_ui_snapshot_smoke PRIVATE "${AUDIOCITY_ASIO_HOST_DIR}")
    target_include_directories(audiocity_ui_snapshot_header_smoke PRIVATE "${AUDIOCITY_ASIO_HOST_DIR}")
endif ()

target_compile_definitions(audiocity_ui_snapshot_smoke
    PRIVATE
        AUDIOCITY_UI_SNAPSHOT_HARNESS=1
        AUDIOCITY_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        REX_WINDOWS=1
        REX_MAC=0
        JUCE_ENABLE_LIVE_CONSTANT_EDITOR=0
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_ASIO=${AUDIOCITY_JUCE_ASIO}
        JucePlugin_Name="Audiocity"
        JucePlugin_VersionString="${PROJECT_VERSION}")

target_compile_definitions(audiocity_ui_snapshot_header_smoke
    PRIVATE
        AUDIOCITY_UI_SNAPSHOT_HARNESS=1
        AUDIOCITY_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        REX_WINDOWS=1
        REX_MAC=0
        JUCE_ENABLE_LIVE_CONSTANT_EDITOR=0
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_ASIO=${AUDIOCITY_JUCE_ASIO}
        JucePlugin_Name="Audiocity"
        JucePlugin_VersionString="${PROJECT_VERSION}")

target_link_libraries(audiocity_ui_snapshot_smoke
    PRIVATE
        AudiocityBinaryData
        audiocity_rex_sdk
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_data_structures
        juce::juce_dsp
        juce::juce_events
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_gui_extra)

target_link_libraries(audiocity_ui_snapshot_header_smoke
    PRIVATE
        AudiocityBinaryData
        audiocity_rex_sdk
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_data_structures
        juce::juce_dsp
        juce::juce_events
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_gui_extra)

if (MSVC)
    target_compile_options(audiocity_ui_snapshot_smoke PRIVATE /W4 /WX /permissive- /FS)
    target_compile_options(audiocity_ui_snapshot_smoke PRIVATE $<$<CONFIG:Debug>:/Z7>)
    target_compile_options(audiocity_ui_snapshot_header_smoke PRIVATE /W4 /WX /permissive- /FS)
    target_compile_options(audiocity_ui_snapshot_header_smoke PRIVATE $<$<CONFIG:Debug>:/Z7>)
endif ()

set(AUDIOCITY_UI_TEST_VARIANT_SOURCES
    ../src/plugin/PluginProcessor.cpp
    ../src/plugin/PluginAboutPage.cpp
    ../src/plugin/PluginCapturePage.cpp
    ../src/plugin/PluginGeneratePage.cpp
    ../src/plugin/PluginLibraryPage.cpp
    ../src/plugin/PluginMappingPage.cpp
    ../src/plugin/PluginPlayerPage.cpp
    ../src/plugin/PluginSampleHeaderPage.cpp
    ../src/plugin/PluginSampleInfoMetricsLayout.cpp
    ../src/plugin/PluginSampleInspectorCardLayout.cpp
    ../src/plugin/PluginEditor.cpp
    ../src/plugin/PluginInstrumentWorkspace.cpp)

function(audiocity_add_ui_test_support target version_string)
    add_library(${target} STATIC
        ${AUDIOCITY_ENGINE_CORE_TEST_SOURCES}
        ${AUDIOCITY_EXTENDED_TEST_SUPPORT_SOURCES}
        ${AUDIOCITY_UI_TEST_VARIANT_SOURCES})
    target_include_directories(${target}
        PRIVATE
            ../src
            ../third_party/REXSDK_Win_1.9.2
            ${CMAKE_BINARY_DIR}/Audiocity_artefacts/JuceLibraryCode
            ${CMAKE_BINARY_DIR}/juce_binarydata_AudiocityBinaryData/JuceLibraryCode)
    if (NOT AUDIOCITY_ASIO_HEADER_DIR STREQUAL "")
        target_include_directories(${target} PRIVATE "${AUDIOCITY_ASIO_HEADER_DIR}")
    endif ()
    if (NOT AUDIOCITY_ASIO_HOST_DIR STREQUAL "")
        target_include_directories(${target} PRIVATE "${AUDIOCITY_ASIO_HOST_DIR}")
    endif ()
    target_compile_definitions(${target}
        PRIVATE
            AUDIOCITY_UI_SNAPSHOT_HARNESS=1
            AUDIOCITY_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
            REX_WINDOWS=1
            REX_MAC=0
            JUCE_ENABLE_LIVE_CONSTANT_EDITOR=0
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            JUCE_ASIO=${AUDIOCITY_JUCE_ASIO}
            JucePlugin_Name="Audiocity"
            JucePlugin_VersionString="${version_string}")
    audiocity_use_juce_headers(${target} ${AUDIOCITY_UI_TEST_JUCE_MODULES})
    if (MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX /permissive- /FS)
        target_compile_options(${target} PRIVATE $<$<CONFIG:Debug>:/Z7>)
    endif ()
    audiocity_apply_test_instrumentation(${target})
endfunction()

audiocity_add_ui_test_support(audiocity_ui_test_support_current "${PROJECT_VERSION}")
audiocity_add_ui_test_support(audiocity_ui_test_support_compat "1.0.0")

add_executable(audiocity_ui_snapshot_harness
    UiScreenshotHarness.cpp)

add_executable(audiocity_preset_runtime_smoke
    PresetRuntimeSmoke.cpp)

target_include_directories(audiocity_ui_snapshot_harness
    PRIVATE
        ../src
        ../third_party/REXSDK_Win_1.9.2
        ${CMAKE_BINARY_DIR}/Audiocity_artefacts/JuceLibraryCode)

target_include_directories(audiocity_preset_runtime_smoke
    PRIVATE
        ../src
        ../third_party/REXSDK_Win_1.9.2
        ${CMAKE_BINARY_DIR}/Audiocity_artefacts/JuceLibraryCode)

if (NOT AUDIOCITY_ASIO_HEADER_DIR STREQUAL "")
    target_include_directories(audiocity_ui_snapshot_harness PRIVATE "${AUDIOCITY_ASIO_HEADER_DIR}")
    target_include_directories(audiocity_preset_runtime_smoke PRIVATE "${AUDIOCITY_ASIO_HEADER_DIR}")
endif ()

if (NOT AUDIOCITY_ASIO_HOST_DIR STREQUAL "")
    target_include_directories(audiocity_ui_snapshot_harness PRIVATE "${AUDIOCITY_ASIO_HOST_DIR}")
    target_include_directories(audiocity_preset_runtime_smoke PRIVATE "${AUDIOCITY_ASIO_HOST_DIR}")
endif ()

target_compile_definitions(audiocity_ui_snapshot_harness
    PRIVATE
        AUDIOCITY_UI_SNAPSHOT_HARNESS=1
        AUDIOCITY_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        REX_WINDOWS=1
        REX_MAC=0
        JUCE_ENABLE_LIVE_CONSTANT_EDITOR=0
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_ASIO=${AUDIOCITY_JUCE_ASIO}
        JucePlugin_Name="Audiocity"
        JucePlugin_VersionString="${PROJECT_VERSION}")

target_compile_definitions(audiocity_preset_runtime_smoke
    PRIVATE
        AUDIOCITY_UI_SNAPSHOT_HARNESS=1
        AUDIOCITY_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        REX_WINDOWS=1
        REX_MAC=0
        JUCE_ENABLE_LIVE_CONSTANT_EDITOR=0
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_ASIO=${AUDIOCITY_JUCE_ASIO}
        JucePlugin_Name="Audiocity"
        JucePlugin_VersionString="${PROJECT_VERSION}")

target_link_libraries(audiocity_ui_snapshot_harness
    PRIVATE
        audiocity_ui_test_support_current
        AudiocityBinaryData
        audiocity_rex_sdk
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_data_structures
        juce::juce_dsp
        juce::juce_events
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_gui_extra)

target_link_libraries(audiocity_preset_runtime_smoke
    PRIVATE
        audiocity_ui_test_support_current
        AudiocityBinaryData
        audiocity_rex_sdk
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_data_structures
        juce::juce_dsp
        juce::juce_events
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_gui_extra)

if (MSVC)
    target_compile_options(audiocity_ui_snapshot_harness PRIVATE /W4 /WX /permissive- /FS)
    target_compile_options(audiocity_ui_snapshot_harness PRIVATE $<$<CONFIG:Debug>:/Z7>)
    target_compile_options(audiocity_preset_runtime_smoke PRIVATE /W4 /WX /permissive- /FS)
    target_compile_options(audiocity_preset_runtime_smoke PRIVATE $<$<CONFIG:Debug>:/Z7>)
endif ()

audiocity_apply_test_instrumentation(audiocity_ui_snapshot_harness)
audiocity_apply_test_instrumentation(audiocity_preset_runtime_smoke)

add_executable(audiocity_ui_snapshot_core_smoke
    UiSnapshotSmoke.cpp)

target_include_directories(audiocity_ui_snapshot_core_smoke
    PRIVATE
        ../src
        ../third_party/REXSDK_Win_1.9.2
        ${CMAKE_BINARY_DIR}/Audiocity_artefacts/JuceLibraryCode)

if (NOT AUDIOCITY_ASIO_HEADER_DIR STREQUAL "")
    target_include_directories(audiocity_ui_snapshot_core_smoke PRIVATE "${AUDIOCITY_ASIO_HEADER_DIR}")
endif ()

if (NOT AUDIOCITY_ASIO_HOST_DIR STREQUAL "")
    target_include_directories(audiocity_ui_snapshot_core_smoke PRIVATE "${AUDIOCITY_ASIO_HOST_DIR}")
endif ()

target_compile_definitions(audiocity_ui_snapshot_core_smoke
    PRIVATE
        AUDIOCITY_UI_SNAPSHOT_HARNESS=1
        AUDIOCITY_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        REX_WINDOWS=1
        REX_MAC=0
        JUCE_ENABLE_LIVE_CONSTANT_EDITOR=0
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_ASIO=${AUDIOCITY_JUCE_ASIO}
        JucePlugin_Name="Audiocity"
        JucePlugin_VersionString="1.0.0")

target_link_libraries(audiocity_ui_snapshot_core_smoke
    PRIVATE
        audiocity_ui_test_support_compat
        AudiocityBinaryData
        audiocity_rex_sdk
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_data_structures
        juce::juce_dsp
        juce::juce_events
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_gui_extra)

if (MSVC)
    target_compile_options(audiocity_ui_snapshot_core_smoke PRIVATE /W4 /WX /permissive- /FS)
    target_compile_options(audiocity_ui_snapshot_core_smoke PRIVATE $<$<CONFIG:Debug>:/Z7>)
endif ()

audiocity_apply_test_instrumentation(audiocity_ui_snapshot_core_smoke)

set(AUDIOCITY_UI_SNAPSHOT_EXE "$<TARGET_FILE:audiocity_ui_snapshot_harness>")

add_test(
    NAME audiocity_ui_snapshot_harness
    COMMAND "${CMAKE_COMMAND}"
        -DAUDIOCITY_SOURCE_DIR=${CMAKE_SOURCE_DIR}
        -DAUDIOCITY_UI_SNAPSHOT_EXE=${AUDIOCITY_UI_SNAPSHOT_EXE}
        -DAUDIOCITY_UI_SNAPSHOT_DIR=${CMAKE_BINARY_DIR}/ui-snapshots
        -DAUDIOCITY_UI_SNAPSHOT_BASELINE_DIR=${CMAKE_CURRENT_LIST_DIR}/ui-snapshot-baselines/current
        -P "${CMAKE_CURRENT_LIST_DIR}/RunUiSnapshotHarness.cmake")

add_test(
    NAME audiocity_preset_runtime_smoke
    COMMAND "$<TARGET_FILE:audiocity_preset_runtime_smoke>")

add_test(
    NAME audiocity_ui_snapshot_header_smoke
    COMMAND "$<TARGET_FILE:audiocity_ui_snapshot_header_smoke>")

add_test(
    NAME audiocity_ui_snapshot_core_smoke
    COMMAND "$<TARGET_FILE:audiocity_ui_snapshot_core_smoke>")

set_tests_properties(
    audiocity_ui_snapshot_header_smoke
    audiocity_ui_snapshot_core_smoke
    PROPERTIES LABELS "ui;smoke;compatibility")

