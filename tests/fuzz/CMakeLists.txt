cmake_minimum_required(VERSION 3.22)

project(AudiocityImporterFuzz LANGUAGES CXX)

include("${CMAKE_CURRENT_LIST_DIR}/../ImporterFuzzTargets.cmake")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "The standalone importer fuzz build requires Clang/libFuzzer")
endif ()

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

get_filename_component(AUDIOCITY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

add_library(audiocity_importer_fuzz_support STATIC
    ${AUDIOCITY_ROOT}/src/engine/AudioFileSupport.cpp
    ${AUDIOCITY_ROOT}/src/engine/LegacyNkiProbe.cpp
    ${AUDIOCITY_ROOT}/src/engine/SfzImporter.cpp
    ${AUDIOCITY_ROOT}/src/engine/Sf2Importer.cpp
    ${AUDIOCITY_ROOT}/src/engine/DecentSamplerImporter.cpp
    ${AUDIOCITY_ROOT}/src/engine/BitwigMultisampleImporter.cpp
    ${AUDIOCITY_ROOT}/src/engine/XmlMultisampleImporterUtils.cpp
    ${AUDIOCITY_ROOT}/src/engine/XmlMultisampleImporters.cpp
    ${AUDIOCITY_ROOT}/src/engine/BinaryMultisampleImporters.cpp)

target_include_directories(audiocity_importer_fuzz_support PRIVATE ${AUDIOCITY_ROOT}/src)
target_compile_definitions(audiocity_importer_fuzz_support
    PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_compile_options(audiocity_importer_fuzz_support
    PRIVATE -fsanitize=fuzzer-no-link,address,undefined -fno-omit-frame-pointer)
target_link_libraries(audiocity_importer_fuzz_support
    PRIVATE
        juce::juce_audio_formats
        juce::juce_data_structures)

enable_testing()
foreach (audiocity_fuzz_format IN LISTS AUDIOCITY_IMPORTER_FUZZ_FORMATS)
    set(audiocity_fuzz_target "audiocity_fuzz_${audiocity_fuzz_format}")
    audiocity_prepare_importer_fuzz_corpus(
        "${audiocity_fuzz_format}"
        audiocity_fuzz_corpus_directory
        audiocity_fuzz_corpus_inputs)
    add_executable(${audiocity_fuzz_target} ${AUDIOCITY_ROOT}/tests/ImporterFuzzHarness.cpp)
    target_include_directories(${audiocity_fuzz_target} PRIVATE
        ${AUDIOCITY_ROOT}/src
        $<TARGET_PROPERTY:juce::juce_core,INTERFACE_INCLUDE_DIRECTORIES>)
    target_compile_definitions(${audiocity_fuzz_target}
        PRIVATE
            AUDIOCITY_FUZZ_FORMAT="${audiocity_fuzz_format}"
            AUDIOCITY_LIBFUZZER=1
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0)
    target_compile_options(${audiocity_fuzz_target}
        PRIVATE -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer)
    target_link_options(${audiocity_fuzz_target}
        PRIVATE -fsanitize=fuzzer,address,undefined)
    target_link_libraries(${audiocity_fuzz_target} PRIVATE audiocity_importer_fuzz_support)

    add_test(NAME ${audiocity_fuzz_target}_corpus
        COMMAND ${audiocity_fuzz_target}
            "${audiocity_fuzz_corpus_directory}"
            -runs=1000 -seed=424242 -max_len=65536 -timeout=10)
    set_tests_properties(${audiocity_fuzz_target}_corpus
        PROPERTIES LABELS "asan;ubsan;fuzz;import" TIMEOUT 45)
endforeach ()
