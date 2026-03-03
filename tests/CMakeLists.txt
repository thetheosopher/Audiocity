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

if (MSVC)
    set(AUDIOCITY_OFFLINE_TEST_EXE "${CMAKE_BINARY_DIR}/tests/Debug/audiocity_offline_tests.exe")
else ()
    set(AUDIOCITY_OFFLINE_TEST_EXE "$<TARGET_FILE:audiocity_offline_tests>")
endif ()

add_test(NAME audiocity_offline_tests COMMAND "${AUDIOCITY_OFFLINE_TEST_EXE}")
add_test(NAME c:/projects/other/audiocity COMMAND "${AUDIOCITY_OFFLINE_TEST_EXE}")
