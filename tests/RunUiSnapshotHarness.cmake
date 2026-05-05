if (NOT DEFINED AUDIOCITY_UI_SNAPSHOT_EXE OR AUDIOCITY_UI_SNAPSHOT_EXE STREQUAL "")
    message(FATAL_ERROR "AUDIOCITY_UI_SNAPSHOT_EXE was not provided.")
endif ()

if (NOT DEFINED AUDIOCITY_UI_SNAPSHOT_DIR OR AUDIOCITY_UI_SNAPSHOT_DIR STREQUAL "")
    message(FATAL_ERROR "AUDIOCITY_UI_SNAPSHOT_DIR was not provided.")
endif ()

if (NOT DEFINED AUDIOCITY_SOURCE_DIR OR AUDIOCITY_SOURCE_DIR STREQUAL "")
    message(FATAL_ERROR "AUDIOCITY_SOURCE_DIR was not provided.")
endif ()

set(candidate_paths "${AUDIOCITY_UI_SNAPSHOT_EXE}")

get_filename_component(requested_config_dir "${AUDIOCITY_UI_SNAPSHOT_EXE}" DIRECTORY)
get_filename_component(test_binary_name "${AUDIOCITY_UI_SNAPSHOT_EXE}" NAME)
get_filename_component(test_binary_parent "${requested_config_dir}" DIRECTORY)

foreach (config_name IN ITEMS Debug Release RelWithDebInfo MinSizeRel)
    list(APPEND candidate_paths "${test_binary_parent}/${config_name}/${test_binary_name}")
endforeach ()

set(test_executable "")
foreach (candidate_path IN LISTS candidate_paths)
    if (EXISTS "${candidate_path}")
        set(test_executable "${candidate_path}")
        break()
    endif ()
endforeach ()

if (test_executable STREQUAL "")
    message(FATAL_ERROR "Unable to find audiocity UI snapshot harness executable. Tried: ${candidate_paths}")
endif ()

execute_process(
    COMMAND "${test_executable}" --output-dir "${AUDIOCITY_UI_SNAPSHOT_DIR}"
    RESULT_VARIABLE test_result)

if (NOT test_result EQUAL 0)
    message(FATAL_ERROR "Audiocity UI snapshot harness failed with exit code ${test_result}.")
endif ()

if (DEFINED AUDIOCITY_UI_SNAPSHOT_BASELINE_DIR AND NOT AUDIOCITY_UI_SNAPSHOT_BASELINE_DIR STREQUAL "")
    if (NOT EXISTS "${AUDIOCITY_UI_SNAPSHOT_BASELINE_DIR}")
        message(FATAL_ERROR "Audiocity UI snapshot baseline directory not found: ${AUDIOCITY_UI_SNAPSHOT_BASELINE_DIR}")
    endif ()

    find_program(audiocity_powershell_exe NAMES powershell REQUIRED)
    execute_process(
        COMMAND "${audiocity_powershell_exe}"
            -NoProfile
            -ExecutionPolicy Bypass
            -File "${AUDIOCITY_SOURCE_DIR}/scripts/compare_ui_snapshots.ps1"
            -ActualDir "${AUDIOCITY_UI_SNAPSHOT_DIR}"
            -BaselineDir "${AUDIOCITY_UI_SNAPSHOT_BASELINE_DIR}"
            -ReportDir "${AUDIOCITY_UI_SNAPSHOT_DIR}"
        RESULT_VARIABLE compare_result)

    if (NOT compare_result EQUAL 0)
        message(FATAL_ERROR "Audiocity UI snapshot baseline comparison failed with exit code ${compare_result}.")
    endif ()
endif ()