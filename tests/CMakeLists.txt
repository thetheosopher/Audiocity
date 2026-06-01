set(AUDIOCITY_ENGINE_TEST_COMMON_SOURCES
    ../src/plugin/PeakPreviewCache.cpp
    ../src/plugin/ImportedProgramState.cpp
    ../src/plugin/PresetJson.cpp
    ../src/plugin/ProgramMappingModel.cpp
    ../src/plugin/CcLearnDial.cpp
    ../src/plugin/PlayerPadState.cpp
    ../src/plugin/SampleBrowserTooltip.cpp
    ../src/plugin/LibraryFileIndex.cpp
    ../src/plugin/LibraryMetadata.cpp
    ../src/engine/AudioFileSupport.cpp
    ../src/engine/EngineCore.cpp
    ../src/engine/LegacyNkiProbe.cpp
    ../src/engine/RexLoader.cpp
    ../src/engine/SfzImporter.cpp
    ../src/engine/SfzExporter.cpp
    ../src/engine/Sf2Importer.cpp
    ../src/engine/DecentSamplerImporter.cpp
    ../src/engine/DecentSamplerExporter.cpp
    ../src/engine/BitwigMultisampleImporter.cpp
    ../src/engine/XmlMultisampleImporterUtils.cpp
    ../src/engine/XmlMultisampleImporters.cpp
    ../src/engine/BinaryMultisampleImporters.cpp
    ../src/engine/VoicePool.cpp)

add_executable(audiocity_offline_tests
    OfflineRenderTests.cpp
    ${AUDIOCITY_ENGINE_TEST_COMMON_SOURCES})

set(AUDIOCITY_ENGINE_PROFILE_SOURCES
    ../src/engine/AudioFileSupport.cpp
    ../src/engine/EngineCore.cpp
    ../src/engine/RexLoader.cpp
    ../src/engine/VoicePool.cpp)

add_executable(audiocity_engine_profile
    EngineProfileHarness.cpp
    ${AUDIOCITY_ENGINE_PROFILE_SOURCES})

foreach (audiocity_engine_target IN ITEMS audiocity_offline_tests audiocity_engine_profile)
    target_include_directories(${audiocity_engine_target}
        PRIVATE
            ../src
            ../third_party/REXSDK_Win_1.9.2)

    target_link_libraries(${audiocity_engine_target}
        PRIVATE
            audiocity_rex_sdk)

    target_compile_definitions(${audiocity_engine_target}
        PRIVATE
            AUDIOCITY_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
            REX_WINDOWS=1
            REX_MAC=0)

    target_link_libraries(${audiocity_engine_target}
        PRIVATE
            juce::juce_gui_basics
            juce::juce_audio_basics
            juce::juce_audio_formats
            juce::juce_data_structures
            juce::juce_dsp
            juce::juce_events
            juce::juce_core)

    if (MSVC)
        target_compile_options(${audiocity_engine_target} PRIVATE /W4 /WX /permissive-)
    endif ()
endforeach ()

target_link_libraries(audiocity_engine_profile
    PRIVATE
        juce::juce_gui_basics)

set(AUDIOCITY_OFFLINE_TEST_EXE "$<TARGET_FILE:audiocity_offline_tests>")

add_test(
    NAME audiocity_offline_tests
    COMMAND "${CMAKE_COMMAND}"
        -DAUDIOCITY_TEST_EXE=${AUDIOCITY_OFFLINE_TEST_EXE}
        -P "${CMAKE_CURRENT_LIST_DIR}/RunOfflineTestExecutable.cmake")

add_test(
    NAME c:/projects/other/audiocity
    COMMAND "${CMAKE_COMMAND}"
        -DAUDIOCITY_TEST_EXE=${AUDIOCITY_OFFLINE_TEST_EXE}
        -P "${CMAKE_CURRENT_LIST_DIR}/RunOfflineTestExecutable.cmake")

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

set(AUDIOCITY_UI_SNAPSHOT_SHARED_SOURCES
    ../src/plugin/PluginProcessor.cpp
    ../src/plugin/PluginAboutPage.cpp
    ../src/plugin/PluginCapturePage.cpp
    ../src/plugin/PluginGeneratePage.cpp
    ../src/plugin/PluginEditor.cpp
    ../src/plugin/PeakPreviewCache.cpp
    ../src/plugin/ImportedProgramState.cpp
    ../src/plugin/PresetJson.cpp
    ../src/plugin/ProgramMappingModel.cpp
    ../src/plugin/PlayerPadState.cpp
    ../src/plugin/SampleBrowserTooltip.cpp
    ../src/plugin/CcLearnDial.cpp
    ../src/plugin/LibraryFileIndex.cpp
    ../src/plugin/LibraryMetadata.cpp
    ../src/engine/AudioFileSupport.cpp
    ../src/engine/EngineCore.cpp
    ../src/engine/LegacyNkiProbe.cpp
    ../src/engine/RexLoader.cpp
    ../src/engine/SfzImporter.cpp
    ../src/engine/SfzExporter.cpp
    ../src/engine/Sf2Importer.cpp
    ../src/engine/DecentSamplerImporter.cpp
    ../src/engine/DecentSamplerExporter.cpp
    ../src/engine/BitwigMultisampleImporter.cpp
    ../src/engine/XmlMultisampleImporterUtils.cpp
    ../src/engine/XmlMultisampleImporters.cpp
    ../src/engine/BinaryMultisampleImporters.cpp
    ../src/engine/VoicePool.cpp)

add_executable(audiocity_ui_snapshot_harness
    UiScreenshotHarness.cpp
    ${AUDIOCITY_UI_SNAPSHOT_SHARED_SOURCES})

add_executable(audiocity_preset_runtime_smoke
    PresetRuntimeSmoke.cpp
    ${AUDIOCITY_UI_SNAPSHOT_SHARED_SOURCES})

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

add_executable(audiocity_ui_snapshot_core_smoke
    UiSnapshotSmoke.cpp
    ${AUDIOCITY_UI_SNAPSHOT_SHARED_SOURCES})

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

