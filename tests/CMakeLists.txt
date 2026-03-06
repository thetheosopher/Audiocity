add_executable(audiocity_offline_tests
    OfflineRenderTests.cpp
    ../src/plugin/PeakPreviewCache.cpp
    ../src/plugin/PresetJson.cpp
    ../src/plugin/CcLearnDial.cpp
    ../src/plugin/PlayerPadState.cpp
    ../src/engine/EngineCore.cpp
    ../src/engine/RexLoader.cpp
    ../src/engine/VoicePool.cpp)

target_include_directories(audiocity_offline_tests
    PRIVATE
        ../src
        ../third_party/REXSDK_Win_1.9.2)

target_link_libraries(audiocity_offline_tests
    PRIVATE
        audiocity_rex_sdk)

target_compile_definitions(audiocity_offline_tests
    PRIVATE
        AUDIOCITY_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        REX_WINDOWS=1
        REX_MAC=0)

target_link_libraries(audiocity_offline_tests
    PRIVATE
        juce::juce_gui_basics
        juce::juce_audio_basics
        juce::juce_audio_formats
        juce::juce_data_structures
        juce::juce_dsp
        juce::juce_events
        juce::juce_core)

if (MSVC)
    target_compile_options(audiocity_offline_tests PRIVATE /W4 /WX /permissive-)
endif ()

set(AUDIOCITY_OFFLINE_TEST_EXE "$<TARGET_FILE:audiocity_offline_tests>")

add_test(NAME audiocity_offline_tests COMMAND "${AUDIOCITY_OFFLINE_TEST_EXE}")
add_test(NAME c:/projects/other/audiocity COMMAND "${AUDIOCITY_OFFLINE_TEST_EXE}")

add_test(
    NAME audiocity_packaging_config
    COMMAND "${CMAKE_COMMAND}"
        -DAUDIOCITY_SOURCE_DIR=${CMAKE_SOURCE_DIR}
        -DAUDIOCITY_SELF_CONTAINED_RUNTIME=${AUDIOCITY_SELF_CONTAINED_RUNTIME}
        -DCMAKE_MSVC_RUNTIME_LIBRARY=${CMAKE_MSVC_RUNTIME_LIBRARY}
        -DAUDIOCITY_ENABLE_WIX_INSTALLER=${AUDIOCITY_ENABLE_WIX_INSTALLER}
        -DAUDIOCITY_ENABLE_WIX_BOOTSTRAPPER=${AUDIOCITY_ENABLE_WIX_BOOTSTRAPPER}
        -DAUDIOCITY_ENABLE_CODESIGN=${AUDIOCITY_ENABLE_CODESIGN}
        -DAUDIOCITY_CODESIGN_CERT_SHA1=${AUDIOCITY_CODESIGN_CERT_SHA1}
        -DAUDIOCITY_SIGNTOOL_EXECUTABLE=${AUDIOCITY_SIGNTOOL_EXECUTABLE}
        -P "${CMAKE_CURRENT_LIST_DIR}/PackagingConfigTests.cmake")

if (WIN32 AND AUDIOCITY_ENABLE_WIX_INSTALLER)
    find_program(AUDIOCITY_POWERSHELL_EXECUTABLE NAMES pwsh powershell)
    if (AUDIOCITY_POWERSHELL_EXECUTABLE)
        add_test(
            NAME audiocity_packaging_msi_tables
            COMMAND "${AUDIOCITY_POWERSHELL_EXECUTABLE}"
                -NoProfile
                -ExecutionPolicy
                Bypass
                -File "${CMAKE_CURRENT_LIST_DIR}/ValidateInstallerMsiTables.ps1"
                -InstallerRoot "${CMAKE_SOURCE_DIR}/output/installer")
    endif ()
endif ()
