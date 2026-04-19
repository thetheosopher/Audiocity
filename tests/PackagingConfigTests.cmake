if (DEFINED AUDIOCITY_SOURCE_DIR AND NOT AUDIOCITY_SOURCE_DIR STREQUAL "")
    set(_audiocity_source_dir "${AUDIOCITY_SOURCE_DIR}")
else ()
    get_filename_component(_audiocity_source_dir "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
endif ()

file(READ "${_audiocity_source_dir}/CMakePresets.json" _audiocity_presets)
file(READ "${_audiocity_source_dir}/installer/AudiocityInstaller.iss" _audiocity_installer_iss)
file(READ "${_audiocity_source_dir}/scripts/build_release.ps1" _audiocity_build_release)
file(READ "${_audiocity_source_dir}/installer/PortableInstall.txt" _audiocity_portable_install)
file(READ "${_audiocity_source_dir}/.vscode/tasks.json" _audiocity_vscode_tasks)
file(READ "${_audiocity_source_dir}/.vscode/launch.json" _audiocity_vscode_launch)
file(READ "${_audiocity_source_dir}/LICENSE" _audiocity_license)

if (NOT _audiocity_presets MATCHES "\"name\"[^\r\n]*\"release-selfcontained\"")
    message(FATAL_ERROR "CMakePresets.json must define a release-selfcontained configure/build preset.")
endif ()

if (NOT _audiocity_presets MATCHES "AUDIOCITY_SELF_CONTAINED_RUNTIME\"[ \t\r\n]*:[ \t\r\n]*\"ON\"")
    message(FATAL_ERROR "CMakePresets.json must enable AUDIOCITY_SELF_CONTAINED_RUNTIME for release packaging.")
endif ()

if (NOT _audiocity_presets MATCHES "build/release-selfcontained")
    message(FATAL_ERROR "CMakePresets.json must isolate the self-contained release build in build/release-selfcontained.")
endif ()

if (NOT _audiocity_installer_iss MATCHES "#define MyAppVersion \"1\\.0\\.0\"")
    message(FATAL_ERROR "AudiocityInstaller.iss must define MyAppVersion as 1.0.0.")
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

if (NOT _audiocity_build_release MATCHES "'--preset', \\$configurePreset")
    message(FATAL_ERROR "build_release.ps1 must configure CMake using the selected self-contained preset.")
endif ()

if (NOT _audiocity_build_release MATCHES "Compress-Archive")
    message(FATAL_ERROR "build_release.ps1 must create the portable zip with Compress-Archive.")
endif ()

if (NOT _audiocity_build_release MATCHES "ISCC\\.exe")
    message(FATAL_ERROR "build_release.ps1 must discover or use the Inno Setup compiler (ISCC.exe).")
endif ()

if (NOT _audiocity_build_release MATCHES "\\$appVersion = '1\\.0\\.0'")
    message(FATAL_ERROR "build_release.ps1 must set appVersion to 1.0.0.")
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

if (NOT _audiocity_vscode_tasks MATCHES "cmake --preset default")
    message(FATAL_ERROR ".vscode/tasks.json must configure the default preset before building in Debug.")
endif ()

if (NOT _audiocity_vscode_tasks MATCHES "Remove-Item -LiteralPath 'build' -Recurse -Force")
    message(FATAL_ERROR ".vscode/tasks.json must delete the stale build directory before retrying the default configure.")
endif ()

if (NOT _audiocity_vscode_tasks MATCHES "cmake --build --preset default --config Debug --target Audiocity_All")
    message(FATAL_ERROR ".vscode/tasks.json must build Audiocity_All from the default Debug preset.")
endif ()

if (NOT _audiocity_vscode_tasks MATCHES "cmake --build --preset default --config Debug --target audiocity_offline_tests")
    message(FATAL_ERROR ".vscode/tasks.json must build audiocity_offline_tests from the default Debug preset.")
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
