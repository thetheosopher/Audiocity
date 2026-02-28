add_executable(audiocity_offline_tests
    OfflineRenderTests.cpp
    ../src/engine/EngineCore.cpp
    ../src/engine/VoicePool.cpp)

target_include_directories(audiocity_offline_tests
    PRIVATE
        ../src)

target_compile_definitions(audiocity_offline_tests
    PRIVATE
        AUDIOCITY_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

target_link_libraries(audiocity_offline_tests
    PRIVATE
        juce::juce_audio_basics
        juce::juce_audio_formats
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
