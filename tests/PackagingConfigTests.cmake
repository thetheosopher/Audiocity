if (DEFINED AUDIOCITY_SOURCE_DIR AND NOT AUDIOCITY_SOURCE_DIR STREQUAL "")
    set(_audiocity_source_dir "${AUDIOCITY_SOURCE_DIR}")
else ()
    get_filename_component(_audiocity_source_dir "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
endif ()

file(READ "${_audiocity_source_dir}/CMakePresets.json" _audiocity_presets)
file(READ "${_audiocity_source_dir}/CMakeLists.txt" _audiocity_root_cmake)
file(READ "${_audiocity_source_dir}/installer/AudiocityInstaller.iss" _audiocity_installer_iss)
file(READ "${_audiocity_source_dir}/scripts/build_release.ps1" _audiocity_build_release)
file(READ "${_audiocity_source_dir}/installer/PortableInstall.txt" _audiocity_portable_install)
file(READ "${_audiocity_source_dir}/.vscode/tasks.json" _audiocity_vscode_tasks)
file(READ "${_audiocity_source_dir}/.vscode/launch.json" _audiocity_vscode_launch)
file(READ "${_audiocity_source_dir}/scripts/export_ui_snapshots.ps1" _audiocity_export_ui_snapshots)
file(READ "${_audiocity_source_dir}/scripts/compare_ui_snapshots.ps1" _audiocity_compare_ui_snapshots)
file(READ "${_audiocity_source_dir}/.github/workflows/ui-snapshots.yml" _audiocity_ui_snapshot_workflow)
file(READ "${_audiocity_source_dir}/tests/RunUiSnapshotHarness.cmake" _audiocity_run_ui_snapshot_harness)
file(READ "${_audiocity_source_dir}/LICENSE" _audiocity_license)
file(GLOB _audiocity_ui_snapshot_baselines "${_audiocity_source_dir}/tests/ui-snapshot-baselines/current/*.png")

string(REGEX MATCH "project[^\n\r]*Audiocity[ \t]+VERSION[ \t]+([0-9]+[.][0-9]+([.][0-9]+([.][0-9]+)?)?)"
    _audiocity_version_match
    "${_audiocity_root_cmake}")
set(_audiocity_version "${CMAKE_MATCH_1}")

if (_audiocity_version STREQUAL "")
    message(FATAL_ERROR "CMakeLists.txt must declare an Audiocity project version.")
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
