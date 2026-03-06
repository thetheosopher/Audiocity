if (AUDIOCITY_SELF_CONTAINED_RUNTIME)
    if (NOT CMAKE_MSVC_RUNTIME_LIBRARY MATCHES "^MultiThreaded")
        message(FATAL_ERROR "AUDIOCITY_SELF_CONTAINED_RUNTIME is ON but CMAKE_MSVC_RUNTIME_LIBRARY is '${CMAKE_MSVC_RUNTIME_LIBRARY}', expected MultiThreaded*.")
    endif ()
endif ()

if (AUDIOCITY_ENABLE_WIX_BOOTSTRAPPER)
    if (NOT AUDIOCITY_ENABLE_WIX_INSTALLER)
        message(FATAL_ERROR "AUDIOCITY_ENABLE_WIX_BOOTSTRAPPER requires AUDIOCITY_ENABLE_WIX_INSTALLER.")
    endif ()
endif ()

if (DEFINED AUDIOCITY_SOURCE_DIR AND NOT AUDIOCITY_SOURCE_DIR STREQUAL "")
    set(_audiocity_source_dir "${AUDIOCITY_SOURCE_DIR}")
else ()
    get_filename_component(_audiocity_source_dir "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
endif ()

if (AUDIOCITY_ENABLE_WIX_INSTALLER)
    file(READ "${_audiocity_source_dir}/installer/AudiocityInstaller.wxs" _audiocity_installer_wxs)
    file(READ "${_audiocity_source_dir}/installer/AudiocityInstaller.wixproj" _audiocity_installer_wixproj)

    if (NOT _audiocity_installer_wxs MATCHES "InstallScope=\"perMachine\"")
        message(FATAL_ERROR "AudiocityInstaller.wxs must keep InstallScope=\"perMachine\" for machine-default scope.")
    endif ()

    if (NOT _audiocity_installer_wxs MATCHES "InstallPrivileges=\"elevated\"")
        message(FATAL_ERROR "AudiocityInstaller.wxs must keep InstallPrivileges=\"elevated\" with perMachine scope.")
    endif ()

    if (NOT _audiocity_installer_wxs MATCHES "<Property Id=\"WixUISupportPerUser\" Value=\"1\" />")
        message(FATAL_ERROR "AudiocityInstaller.wxs must enable WixUISupportPerUser.")
    endif ()

    if (NOT _audiocity_installer_wxs MATCHES "<Property Id=\"WixUISupportPerMachine\" Value=\"1\" />")
        message(FATAL_ERROR "AudiocityInstaller.wxs must enable WixUISupportPerMachine.")
    endif ()

    if (NOT _audiocity_installer_wixproj MATCHES "<SuppressIces>ICE91</SuppressIces>")
        message(FATAL_ERROR "AudiocityInstaller.wixproj must suppress ICE91 for expected dual-scope per-user directory authoring.")
    endif ()
endif ()

if (AUDIOCITY_ENABLE_WIX_BOOTSTRAPPER)
    file(READ "${_audiocity_source_dir}/installer/AudiocityBootstrapper.wxs" _audiocity_bootstrapper_wxs)

    if (NOT _audiocity_bootstrapper_wxs MATCHES "DisplayInternalUI=\"yes\"")
        message(FATAL_ERROR "AudiocityBootstrapper.wxs must use DisplayInternalUI=\"yes\" so MSI scope UI is available.")
    endif ()
endif ()

if (AUDIOCITY_ENABLE_CODESIGN)
    if (AUDIOCITY_CODESIGN_CERT_SHA1 STREQUAL "")
        message(FATAL_ERROR "AUDIOCITY_ENABLE_CODESIGN requires AUDIOCITY_CODESIGN_CERT_SHA1.")
    endif ()

    if (AUDIOCITY_SIGNTOOL_EXECUTABLE STREQUAL "")
        message(FATAL_ERROR "AUDIOCITY_ENABLE_CODESIGN requires AUDIOCITY_SIGNTOOL_EXECUTABLE.")
    endif ()
endif ()