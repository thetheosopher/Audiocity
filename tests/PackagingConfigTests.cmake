if (DEFINED AUDIOCITY_SOURCE_DIR AND NOT AUDIOCITY_SOURCE_DIR STREQUAL "")
    set(_audiocity_source_dir "${AUDIOCITY_SOURCE_DIR}")
else ()
    get_filename_component(_audiocity_source_dir "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
endif ()

file(READ "${_audiocity_source_dir}/CMakePresets.json" _audiocity_presets)
file(READ "${_audiocity_source_dir}/CMakeLists.txt" _audiocity_root_cmake)
file(READ "${_audiocity_source_dir}/README.md" _audiocity_readme)
file(READ "${_audiocity_source_dir}/docs/USER_GUIDE.md" _audiocity_user_guide)
file(READ "${_audiocity_source_dir}/installer/AudiocityInstaller.iss" _audiocity_installer_iss)
file(READ "${_audiocity_source_dir}/scripts/build_release.ps1" _audiocity_build_release)
file(READ "${_audiocity_source_dir}/scripts/verify_release_artifacts.ps1" _audiocity_verify_release_artifacts)
file(READ "${_audiocity_source_dir}/scripts/bootstrap.ps1" _audiocity_bootstrap)
file(READ "${_audiocity_source_dir}/installer/PortableInstall.txt" _audiocity_portable_install)
file(READ "${_audiocity_source_dir}/.vscode/tasks.json" _audiocity_vscode_tasks)
file(READ "${_audiocity_source_dir}/.vscode/launch.json" _audiocity_vscode_launch)
file(READ "${_audiocity_source_dir}/scripts/export_ui_snapshots.ps1" _audiocity_export_ui_snapshots)
file(READ "${_audiocity_source_dir}/scripts/compare_ui_snapshots.ps1" _audiocity_compare_ui_snapshots)
file(READ "${_audiocity_source_dir}/src/plugin/PluginEditor.cpp" _audiocity_plugin_editor)
file(READ "${_audiocity_source_dir}/src/plugin/PluginProcessor.cpp" _audiocity_plugin_processor)
file(READ "${_audiocity_source_dir}/src/plugin/PluginProcessor.h" _audiocity_plugin_processor_header)
file(READ "${_audiocity_source_dir}/src/plugin/OwnedJobWorker.h" _audiocity_owned_job_worker)
file(READ "${_audiocity_source_dir}/src/plugin/EngineProgramSink.h" _audiocity_engine_program_sink)
file(READ "${_audiocity_source_dir}/src/engine/EngineCore.cpp" _audiocity_engine_core)
file(READ "${_audiocity_source_dir}/src/engine/EngineCore.h" _audiocity_engine_core_header)
file(READ "${_audiocity_source_dir}/src/engine/RtSnapshotCell.h" _audiocity_rt_snapshot_cell)
file(READ "${_audiocity_source_dir}/docs/02-real-time-rules.md" _audiocity_rt_rules)
file(READ "${_audiocity_source_dir}/.github/workflows/ui-snapshots.yml" _audiocity_ui_snapshot_workflow)
file(READ "${_audiocity_source_dir}/.github/workflows/build-and-test.yml" _audiocity_build_and_test_workflow)
file(READ "${_audiocity_source_dir}/tests/RunUiSnapshotHarness.cmake" _audiocity_run_ui_snapshot_harness)
file(READ "${_audiocity_source_dir}/LICENSE" _audiocity_license)
file(GLOB _audiocity_factory_presets "${_audiocity_source_dir}/assets/factory_presets/*.acp")
file(GLOB _audiocity_ui_snapshot_baselines "${_audiocity_source_dir}/tests/ui-snapshot-baselines/current/*.png")

list(LENGTH _audiocity_factory_presets _audiocity_factory_preset_count)

if (NOT _audiocity_readme MATCHES "curated ${_audiocity_factory_preset_count}-preset factory bank")
    message(FATAL_ERROR "README.md factory-preset count must match the ${_audiocity_factory_preset_count} shipped .acp files.")
endif ()

if (NOT _audiocity_user_guide MATCHES "stock bank of ${_audiocity_factory_preset_count} factory presets")
    message(FATAL_ERROR "docs/USER_GUIDE.md factory-preset count must match the ${_audiocity_factory_preset_count} shipped .acp files.")
endif ()

string(REGEX MATCH "project[^\n\r]*Audiocity[ \t]+VERSION[ \t]+([0-9]+[.][0-9]+([.][0-9]+([.][0-9]+)?)?)"
    _audiocity_version_match
    "${_audiocity_root_cmake}")
set(_audiocity_version "${CMAKE_MATCH_1}")

if (_audiocity_version STREQUAL "")
    message(FATAL_ERROR "CMakeLists.txt must declare an Audiocity project version.")
endif ()

if (NOT _audiocity_presets MATCHES "\"name\"[^\r\n]*\"ci-windows\"")
    message(FATAL_ERROR "CMakePresets.json must define the ci-windows configure/build/test preset.")
endif ()

if (NOT _audiocity_presets MATCHES "Visual Studio 17 2022")
    message(FATAL_ERROR "The ci-windows preset must target the Visual Studio 2022 generator used by windows-2022 runners.")
endif ()

if (NOT _audiocity_presets MATCHES "build/ci-windows")
    message(FATAL_ERROR "The ci-windows preset must use an isolated build/ci-windows tree.")
endif ()

if (NOT _audiocity_build_and_test_workflow MATCHES "cmake --preset ci-windows")
    message(FATAL_ERROR ".github/workflows/build-and-test.yml must configure with the ci-windows preset.")
endif ()

if (NOT _audiocity_build_and_test_workflow MATCHES "cmake --build --preset ci-windows-release"
    OR NOT _audiocity_presets MATCHES "Audiocity_All")
    message(FATAL_ERROR ".github/workflows/build-and-test.yml must compile the shipped standalone and VST3 products through the Release preset.")
endif ()

if (NOT _audiocity_build_and_test_workflow MATCHES "installer/[*][*]")
    message(FATAL_ERROR ".github/workflows/build-and-test.yml must run for installer-only changes.")
endif ()

if (NOT _audiocity_build_and_test_workflow MATCHES "ci-windows-release")
    message(FATAL_ERROR ".github/workflows/build-and-test.yml must build shipped products in Release configuration.")
endif ()

if (NOT _audiocity_build_and_test_workflow MATCHES "verify_release_artifacts[.]ps1")
    message(FATAL_ERROR ".github/workflows/build-and-test.yml must verify the built product and installer-input paths.")
endif ()

if (NOT _audiocity_build_and_test_workflow MATCHES "verify_release_artifacts[.]ps1[^\r\n]*-CompileInstaller")
    message(FATAL_ERROR ".github/workflows/build-and-test.yml must compile the installer definition against the built Release inputs.")
endif ()

if (NOT _audiocity_verify_release_artifacts MATCHES "Get-ChildItem[^\r\n]*-Recurse"
    OR NOT _audiocity_verify_release_artifacts MATCHES "Get-FileHash[^\r\n]*SHA256"
    OR NOT _audiocity_verify_release_artifacts MATCHES "Assert-PresetBankMatches"
    OR NOT _audiocity_verify_release_artifacts MATCHES "Resolve-InnoSetupCompiler"
    OR NOT _audiocity_verify_release_artifacts MATCHES "/DSourceRoot=[$]artifactRoot")
    message(FATAL_ERROR "verify_release_artifacts.ps1 must compare recursive preset paths/content and support a real ISCC input compile.")
endif ()

if (WIN32)
    find_program(_audiocity_powershell_executable NAMES pwsh powershell)
    if (NOT _audiocity_powershell_executable)
        message(FATAL_ERROR "Packaging verification behavior tests require pwsh or powershell.")
    endif ()

    set(_audiocity_verifier_fixture "${CMAKE_CURRENT_BINARY_DIR}/audiocity-release-verifier-fixture")
    file(REMOVE_RECURSE "${_audiocity_verifier_fixture}")
    file(MAKE_DIRECTORY
        "${_audiocity_verifier_fixture}/scripts"
        "${_audiocity_verifier_fixture}/installer"
        "${_audiocity_verifier_fixture}/assets/icons"
        "${_audiocity_verifier_fixture}/assets/factory_presets/nested"
        "${_audiocity_verifier_fixture}/build/Audiocity_artefacts/Release/Standalone/FactoryPresets/nested"
        "${_audiocity_verifier_fixture}/build/Audiocity_artefacts/Release/VST3/Audiocity.vst3/Contents/x86_64-win"
        "${_audiocity_verifier_fixture}/build/Audiocity_artefacts/Release/VST3/Audiocity.vst3/Contents/Resources/FactoryPresets/nested")
    configure_file(
        "${_audiocity_source_dir}/scripts/verify_release_artifacts.ps1"
        "${_audiocity_verifier_fixture}/scripts/verify_release_artifacts.ps1"
        COPYONLY)

    file(WRITE "${_audiocity_verifier_fixture}/installer/AudiocityInstaller.iss" "; verifier fixture\n")
    file(WRITE "${_audiocity_verifier_fixture}/installer/PortableInstall.txt" "verifier fixture\n")
    file(WRITE "${_audiocity_verifier_fixture}/LICENSE" "verifier fixture\n")
    file(WRITE "${_audiocity_verifier_fixture}/assets/icons/audiocity_icon_multi.ico" "fixture-icon")
    file(WRITE "${_audiocity_verifier_fixture}/assets/factory_presets/root.acp" "root-preset")
    file(WRITE "${_audiocity_verifier_fixture}/assets/factory_presets/nested/nested.acp" "nested-preset")
    file(WRITE "${_audiocity_verifier_fixture}/build/Audiocity_artefacts/Release/Standalone/Audiocity.exe" "fixture-exe")
    file(WRITE "${_audiocity_verifier_fixture}/build/Audiocity_artefacts/Release/VST3/Audiocity.vst3/Contents/x86_64-win/Audiocity.vst3" "fixture-vst3")

    foreach (_audiocity_preset_destination IN ITEMS
        "${_audiocity_verifier_fixture}/build/Audiocity_artefacts/Release/Standalone/FactoryPresets"
        "${_audiocity_verifier_fixture}/build/Audiocity_artefacts/Release/VST3/Audiocity.vst3/Contents/Resources/FactoryPresets")
        file(WRITE "${_audiocity_preset_destination}/root.acp" "root-preset")
        file(WRITE "${_audiocity_preset_destination}/nested/nested.acp" "nested-preset")
    endforeach ()

    set(_audiocity_verifier_command
        "${_audiocity_powershell_executable}" -NoLogo -NoProfile -ExecutionPolicy Bypass
        -File "${_audiocity_verifier_fixture}/scripts/verify_release_artifacts.ps1"
        -BuildDir "${_audiocity_verifier_fixture}/build" -Configuration Release)
    execute_process(
        COMMAND ${_audiocity_verifier_command}
        RESULT_VARIABLE _audiocity_verifier_result
        OUTPUT_VARIABLE _audiocity_verifier_stdout
        ERROR_VARIABLE _audiocity_verifier_stderr)
    if (NOT _audiocity_verifier_result EQUAL 0)
        message(FATAL_ERROR
            "Matching recursive factory-preset banks must pass release verification.\n"
            "stdout:\n${_audiocity_verifier_stdout}\nstderr:\n${_audiocity_verifier_stderr}")
    endif ()

    file(WRITE
        "${_audiocity_verifier_fixture}/build/Audiocity_artefacts/Release/Standalone/FactoryPresets/nested/nested.acp"
        "tampered-preset")
    execute_process(
        COMMAND ${_audiocity_verifier_command}
        RESULT_VARIABLE _audiocity_verifier_result
        OUTPUT_VARIABLE _audiocity_verifier_stdout
        ERROR_VARIABLE _audiocity_verifier_stderr)
    string(CONCAT _audiocity_verifier_output "${_audiocity_verifier_stdout}" "${_audiocity_verifier_stderr}")
    if (_audiocity_verifier_result EQUAL 0 OR NOT _audiocity_verifier_output MATCHES "content differs")
        message(FATAL_ERROR "Release verification must reject same-count Standalone preset content drift.")
    endif ()

    file(WRITE
        "${_audiocity_verifier_fixture}/build/Audiocity_artefacts/Release/Standalone/FactoryPresets/nested/nested.acp"
        "nested-preset")
    file(RENAME
        "${_audiocity_verifier_fixture}/build/Audiocity_artefacts/Release/VST3/Audiocity.vst3/Contents/Resources/FactoryPresets/nested/nested.acp"
        "${_audiocity_verifier_fixture}/build/Audiocity_artefacts/Release/VST3/Audiocity.vst3/Contents/Resources/FactoryPresets/renamed.acp")
    execute_process(
        COMMAND ${_audiocity_verifier_command}
        RESULT_VARIABLE _audiocity_verifier_result
        OUTPUT_VARIABLE _audiocity_verifier_stdout
        ERROR_VARIABLE _audiocity_verifier_stderr)
    string(CONCAT _audiocity_verifier_output "${_audiocity_verifier_stdout}" "${_audiocity_verifier_stderr}")
    if (_audiocity_verifier_result EQUAL 0 OR NOT _audiocity_verifier_output MATCHES "missing 'nested/nested[.]acp'")
        message(FATAL_ERROR "Release verification must reject same-count VST3 preset relative-path drift.")
    endif ()

    file(REMOVE_RECURSE "${_audiocity_verifier_fixture}")
endif ()

if (NOT _audiocity_build_and_test_workflow MATCHES "ctest --preset ci-windows")
    message(FATAL_ERROR ".github/workflows/build-and-test.yml must run tests through the ci-windows test preset.")
endif ()

if (NOT _audiocity_build_and_test_workflow MATCHES "cmake --version"
    OR NOT _audiocity_ui_snapshot_workflow MATCHES "cmake --version")
    message(FATAL_ERROR "Both Windows workflows must print the selected CMake/toolchain version.")
endif ()

if (NOT _audiocity_bootstrap MATCHES "cmake --preset ci-windows")
    message(FATAL_ERROR "scripts/bootstrap.ps1 must use the same VS2022 preset as CI.")
endif ()

if (NOT _audiocity_presets MATCHES "\"name\"[^\r\n]*\"release-selfcontained\"")
    message(FATAL_ERROR "CMakePresets.json must define a release-selfcontained configure/build preset.")
endif ()

if (NOT _audiocity_presets MATCHES "AUDIOCITY_SELF_CONTAINED_RUNTIME\"[ \t\r\n]*:[ \t\r\n]*\"ON\"")
    message(FATAL_ERROR "CMakePresets.json must enable AUDIOCITY_SELF_CONTAINED_RUNTIME for release packaging.")
endif ()

if (NOT _audiocity_presets MATCHES "build/release-selfcontained")
    message(FATAL_ERROR "CMakePresets.json must isolate the self-contained release build in build/release-selfcontained.")
endif ()

if (NOT _audiocity_installer_iss MATCHES "#define MyAppVersion \"${_audiocity_version}\"")
    message(FATAL_ERROR "AudiocityInstaller.iss must define MyAppVersion as ${_audiocity_version}.")
endif ()

if (NOT _audiocity_installer_iss MATCHES "AppVersion=\\{#MyAppVersion\\}")
    message(FATAL_ERROR "AudiocityInstaller.iss must bind AppVersion to MyAppVersion.")
endif ()

if (NOT _audiocity_installer_iss MATCHES "PrivilegesRequiredOverridesAllowed=dialog")
    message(FATAL_ERROR "AudiocityInstaller.iss must allow per-user or per-machine installation via a dialog.")
endif ()

if (NOT _audiocity_installer_iss MATCHES "UninstallDisplayIcon")
    message(FATAL_ERROR "AudiocityInstaller.iss must expose an uninstall icon for Add/Remove Programs.")
endif ()

if (NOT _audiocity_installer_iss MATCHES "Name: \"desktopicon\"")
    message(FATAL_ERROR "AudiocityInstaller.iss must provide an optional desktop shortcut task.")
endif ()

if (NOT _audiocity_installer_iss MATCHES "\\{commoncf64\\}\\\\VST3")
    message(FATAL_ERROR "AudiocityInstaller.iss must install the machine-wide VST3 bundle under Common Files\\VST3.")
endif ()

if (NOT _audiocity_installer_iss MATCHES "\\{localappdata\\}\\\\Programs\\\\Common\\\\VST3")
    message(FATAL_ERROR "AudiocityInstaller.iss must install the per-user VST3 bundle under LocalAppData\\Programs\\Common\\VST3.")
endif ()

if (NOT _audiocity_build_release MATCHES "release-selfcontained")
    message(FATAL_ERROR "build_release.ps1 must reference the release-selfcontained preset.")
endif ()

if (NOT _audiocity_build_release MATCHES "release-selfcontained-asio")
    message(FATAL_ERROR "build_release.ps1 must reference the release-selfcontained-asio preset.")
endif ()

if (NOT _audiocity_build_release MATCHES "Invoke-External 'cmake' @")
    message(FATAL_ERROR "build_release.ps1 must invoke CMake through Invoke-External.")
endif ()

if (NOT _audiocity_build_release MATCHES "Invoke-CMakeConfigureWithRetry -Preset \\$configurePreset")
    message(FATAL_ERROR "build_release.ps1 must configure CMake through the retry helper using the selected self-contained preset.")
endif ()

if (NOT _audiocity_root_cmake MATCHES "TARGET_FILE_DIR:Audiocity_VST3>/[.][.]/Resources/FactoryPresets")
    message(FATAL_ERROR "CMakeLists.txt must copy factory presets into the VST3 bundle's Contents/Resources directory.")
endif ()

if (NOT _audiocity_root_cmake MATCHES "-E rm -rf")
    message(FATAL_ERROR "CMakeLists.txt must clear stale FactoryPresets directories before copying the stock bank.")
endif ()

if (NOT (_audiocity_build_release MATCHES "Reset-Directory"
         AND _audiocity_build_release MATCHES "factoryPresetDestination"))
    message(FATAL_ERROR "build_release.ps1 must reset staged FactoryPresets directories before copying the stock bank.")
endif ()

if (NOT _audiocity_build_release MATCHES "Compress-Archive")
    message(FATAL_ERROR "build_release.ps1 must create the portable zip with Compress-Archive.")
endif ()

if (NOT _audiocity_build_release MATCHES "ISCC\\.exe")
    message(FATAL_ERROR "build_release.ps1 must discover or use the Inno Setup compiler (ISCC.exe).")
endif ()

if (NOT _audiocity_build_release MATCHES "function Get-AppVersion")
    message(FATAL_ERROR "build_release.ps1 must derive appVersion through Get-AppVersion.")
endif ()

if (NOT _audiocity_build_release MATCHES "\\/DMyAppVersion=\\$appVersion")
    message(FATAL_ERROR "build_release.ps1 must pass MyAppVersion through to the installer build.")
endif ()

if (NOT _audiocity_build_release MATCHES "windows-x64-setup\\.exe")
    message(FATAL_ERROR "build_release.ps1 must emit a versioned setup executable artifact.")
endif ()

if (NOT _audiocity_build_release MATCHES "windows-x64-portable\\.zip")
    message(FATAL_ERROR "build_release.ps1 must emit a versioned portable zip artifact.")
endif ()

if (NOT _audiocity_portable_install MATCHES "%LOCALAPPDATA%")
    message(FATAL_ERROR "PortableInstall.txt must document the per-user VST3 copy location.")
endif ()

if (NOT _audiocity_portable_install MATCHES "Programs")
    message(FATAL_ERROR "PortableInstall.txt must document the per-user VST3 folder structure.")
endif ()

if (NOT _audiocity_portable_install MATCHES "Common")
    message(FATAL_ERROR "PortableInstall.txt must document the shared per-user VST3 folder structure.")
endif ()

if (NOT _audiocity_portable_install MATCHES "%CommonProgramFiles%")
    message(FATAL_ERROR "PortableInstall.txt must document the machine-wide VST3 copy location.")
endif ()

if (NOT _audiocity_portable_install MATCHES "VST3")
    message(FATAL_ERROR "PortableInstall.txt must document the VST3 destination folder name.")
endif ()

if (NOT _audiocity_vscode_tasks MATCHES "CMake: Build Audiocity \\(Debug\\)")
    message(FATAL_ERROR ".vscode/tasks.json must define the debug build task.")
endif ()

if (NOT _audiocity_vscode_tasks MATCHES "'cmake\\.exe'")
    message(FATAL_ERROR ".vscode/tasks.json must invoke cmake.exe by name so the workspace can use the resolved CMake installation.")
endif ()

if (NOT _audiocity_vscode_tasks MATCHES "--preset default")
    message(FATAL_ERROR ".vscode/tasks.json must configure the default preset before building in Debug.")
endif ()

if (NOT _audiocity_vscode_tasks MATCHES "build/CMakeCache\\.txt")
    message(FATAL_ERROR ".vscode/tasks.json must clear the stale default CMakeCache.txt before retrying the default configure.")
endif ()

if (NOT _audiocity_vscode_tasks MATCHES "build/CMakeFiles")
    message(FATAL_ERROR ".vscode/tasks.json must clear the stale default CMakeFiles directory before retrying the default configure.")
endif ()

if (NOT _audiocity_vscode_tasks MATCHES "--build --preset default --config Debug --target Audiocity_All")
    message(FATAL_ERROR ".vscode/tasks.json must build Audiocity_All from the default Debug preset.")
endif ()

if (NOT _audiocity_vscode_tasks MATCHES "--build --preset default --config Debug --target audiocity_offline_tests audiocity_ui_snapshot_harness")
    message(FATAL_ERROR ".vscode/tasks.json must build audiocity_offline_tests and audiocity_ui_snapshot_harness from the default Debug preset.")
endif ()

if (NOT _audiocity_vscode_tasks MATCHES "'ctest\\.exe'")
    message(FATAL_ERROR ".vscode/tasks.json must invoke ctest.exe by name for Debug test execution.")
endif ()

string(REGEX MATCHALL "\"label\"[^\r\n]*\"Build UI Snapshot Harness Debug\"" _audiocity_snapshot_task_labels "${_audiocity_vscode_tasks}")
list(LENGTH _audiocity_snapshot_task_labels _audiocity_snapshot_task_label_count)
if (NOT _audiocity_snapshot_task_label_count EQUAL 1)
    message(FATAL_ERROR ".vscode/tasks.json must define exactly one Build UI Snapshot Harness Debug task.")
endif ()

if (NOT _audiocity_export_ui_snapshots MATCHES "audiocity_ui_snapshot_harness")
    message(FATAL_ERROR "scripts/export_ui_snapshots.ps1 must build the audiocity_ui_snapshot_harness target.")
endif ()

if (NOT _audiocity_export_ui_snapshots MATCHES "--smoke-exit")
    message(FATAL_ERROR "scripts/export_ui_snapshots.ps1 must probe the snapshot harness with --smoke-exit before exporting images.")
endif ()

if (NOT _audiocity_export_ui_snapshots MATCHES "snapshot-summary\\.md")
    message(FATAL_ERROR "scripts/export_ui_snapshots.ps1 must emit a snapshot-summary.md artifact for review.")
endif ()

if (NOT _audiocity_export_ui_snapshots MATCHES "compare_ui_snapshots\\.ps1")
    message(FATAL_ERROR "scripts/export_ui_snapshots.ps1 must invoke scripts/compare_ui_snapshots.ps1 when baselines are present.")
endif ()

if (NOT _audiocity_export_ui_snapshots MATCHES "UpdateBaseline")
    message(FATAL_ERROR "scripts/export_ui_snapshots.ps1 must support refreshing committed UI snapshot baselines.")
endif ()

if (NOT _audiocity_compare_ui_snapshots MATCHES "snapshot-diff-summary\\.md")
    message(FATAL_ERROR "scripts/compare_ui_snapshots.ps1 must emit a snapshot-diff-summary.md report.")
endif ()

if (NOT _audiocity_compare_ui_snapshots MATCHES "snapshot-diff-report\\.json")
    message(FATAL_ERROR "scripts/compare_ui_snapshots.ps1 must emit a snapshot-diff-report.json artifact.")
endif ()

if (NOT _audiocity_run_ui_snapshot_harness MATCHES "compare_ui_snapshots\\.ps1")
    message(FATAL_ERROR "tests/RunUiSnapshotHarness.cmake must compare exported snapshots against the committed baseline set.")
endif ()

list(LENGTH _audiocity_ui_snapshot_baselines _audiocity_ui_snapshot_baseline_count)
if (NOT _audiocity_ui_snapshot_baseline_count GREATER 0)
    message(FATAL_ERROR "tests/ui-snapshot-baselines/current must contain committed baseline PNGs.")
endif ()

if (NOT _audiocity_ui_snapshot_workflow MATCHES "scripts/export_ui_snapshots\\.ps1")
    message(FATAL_ERROR ".github/workflows/ui-snapshots.yml must invoke scripts/export_ui_snapshots.ps1.")
endif ()

if (NOT _audiocity_ui_snapshot_workflow MATCHES "CMakePreset ci-windows")
    message(FATAL_ERROR ".github/workflows/ui-snapshots.yml must use the ci-windows preset.")
endif ()

if (NOT _audiocity_ui_snapshot_workflow MATCHES "BuildDir build/ci-windows")
    message(FATAL_ERROR ".github/workflows/ui-snapshots.yml must resolve the harness from the ci-windows build tree.")
endif ()

file(GLOB_RECURSE _audiocity_product_sources
    "${_audiocity_source_dir}/src/*.cpp"
    "${_audiocity_source_dir}/src/*.h")
foreach (_audiocity_product_source IN LISTS _audiocity_product_sources)
    file(READ "${_audiocity_product_source}" _audiocity_product_source_text)
    if (_audiocity_product_source_text MATCHES "[.]detach[(][)]")
        message(FATAL_ERROR "Product source must not launch detached background workers: ${_audiocity_product_source}")
    endif ()
endforeach ()

if (NOT _audiocity_owned_job_worker MATCHES "worker_[.]join[(][)]"
    OR NOT _audiocity_owned_job_worker MATCHES "requestCancellationLocked")
    message(FATAL_ERROR "OwnedJobWorker must cooperatively cancel and join its worker during teardown.")
endif ()

if (NOT _audiocity_plugin_processor_header MATCHES "struct EngineControlSnapshot"
    OR NOT _audiocity_plugin_processor MATCHES "loadEngineControlSnapshot[(][)]"
    OR NOT _audiocity_plugin_processor MATCHES "lastAppliedControls_"
    OR NOT _audiocity_plugin_processor_header MATCHES "panicRequested_"
    OR NOT _audiocity_plugin_processor MATCHES "panicRequested_[.]exchange"
    OR _audiocity_engine_program_sink MATCHES "engine_[.]panic[(][)]"
    OR _audiocity_plugin_processor MATCHES "suspendParamSyncBlocks_")
    message(FATAL_ERROR "PluginProcessor must use the audio-thread-owned snapshot/diff control plane.")
endif ()

if (NOT _audiocity_engine_core MATCHES "publishPreparedSample"
    OR NOT _audiocity_engine_core MATCHES "PublishedProgramSnapshot"
    OR NOT _audiocity_engine_core_header MATCHES "publishedProgramSnapshot_"
    OR NOT _audiocity_engine_core MATCHES "appliedSamplePublishGeneration_"
    OR NOT _audiocity_rt_snapshot_cell MATCHES "readerHazards_"
    OR NOT _audiocity_rt_snapshot_cell MATCHES "readerDepths_"
    OR NOT _audiocity_rt_snapshot_cell MATCHES "readerAcquiring_"
    OR NOT _audiocity_rt_snapshot_cell MATCHES "readerReleasing_"
    OR NOT _audiocity_rt_rules MATCHES "Audio thread only")
    message(FATAL_ERROR "Immutable structural publication and its thread-ownership contract must remain documented.")
endif ()

if (NOT _audiocity_ui_snapshot_workflow MATCHES "actions/upload-artifact@v4")
    message(FATAL_ERROR ".github/workflows/ui-snapshots.yml must upload the snapshot artifact with actions/upload-artifact@v4.")
endif ()

if (NOT _audiocity_ui_snapshot_workflow MATCHES "workflow_dispatch")
    message(FATAL_ERROR ".github/workflows/ui-snapshots.yml must allow manual dispatch for UI review runs.")
endif ()

if (NOT _audiocity_ui_snapshot_workflow MATCHES "BaselineDir")
    message(FATAL_ERROR ".github/workflows/ui-snapshots.yml must compare exported snapshots against the committed baseline directory.")
endif ()

if (NOT _audiocity_vscode_launch MATCHES "\"preLaunchTask\"[ \\t\\r\\n]*:[ \\t\\r\\n]*\"CMake: Build Audiocity \\(Debug\\)\"")
    message(FATAL_ERROR ".vscode/launch.json must launch the debug standalone target through the debug build task.")
endif ()

if (NOT _audiocity_vscode_launch MATCHES "\"preLaunchTask\"[ \\t\\r\\n]*:[ \\t\\r\\n]*\"CMake: Build Tests \\(Debug\\)\"")
    message(FATAL_ERROR ".vscode/launch.json must launch the offline tests through the debug test build task.")
endif ()

if (NOT _audiocity_license MATCHES "MIT License")
    message(FATAL_ERROR "LICENSE must contain the MIT License text.")
endif ()
