if (NOT DEFINED AUDIOCITY_TEST_EXE OR AUDIOCITY_TEST_EXE STREQUAL "")
    message(FATAL_ERROR "AUDIOCITY_TEST_EXE was not provided.")
endif ()

if (NOT EXISTS "${AUDIOCITY_TEST_EXE}")
    message(FATAL_ERROR
        "The generator-selected audiocity offline test executable does not exist: ${AUDIOCITY_TEST_EXE}")
endif ()

set(test_arguments)
if (DEFINED AUDIOCITY_TEST_SUITE AND NOT AUDIOCITY_TEST_SUITE STREQUAL "")
    list(APPEND test_arguments --suite "${AUDIOCITY_TEST_SUITE}")
endif ()

execute_process(
    COMMAND "${AUDIOCITY_TEST_EXE}" ${test_arguments}
    RESULT_VARIABLE test_result)

if (NOT test_result EQUAL 0)
    message(FATAL_ERROR "Audiocity offline tests failed with exit code ${test_result}.")
endif ()
