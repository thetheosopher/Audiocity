cmake_minimum_required(VERSION 3.22)

project(Audiocity VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(AUDIOCITY_FETCH_JUCE "Fetch JUCE automatically when third_party/JUCE is unavailable" ON)

if (EXISTS "${CMAKE_SOURCE_DIR}/third_party/JUCE/CMakeLists.txt")
    add_subdirectory(third_party/JUCE)
elseif (AUDIOCITY_FETCH_JUCE)
    include(FetchContent)
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
else ()
    message(FATAL_ERROR "JUCE not found. Add JUCE at third_party/JUCE or enable AUDIOCITY_FETCH_JUCE.")
endif ()

juce_add_plugin(Audiocity
    COMPANY_NAME "Audiocity"
    BUNDLE_ID "com.audiocity.plugin"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE AuCt
    PLUGIN_CODE AuCy
    FORMATS VST3 Standalone
    ICON_BIG "${CMAKE_SOURCE_DIR}/assets/icons/audiocity_icon_512.png"
    ICON_SMALL "${CMAKE_SOURCE_DIR}/assets/icons/audiocity_icon_128.png"
    PRODUCT_NAME "Audiocity")

juce_generate_juce_header(Audiocity)

add_library(audiocity_rex_sdk STATIC
    third_party/REXSDK_Win_1.9.2/REX.c)

target_include_directories(audiocity_rex_sdk
    PUBLIC
        third_party/REXSDK_Win_1.9.2)

target_compile_definitions(audiocity_rex_sdk
    PRIVATE
        REX_WINDOWS=1
        REX_MAC=0)

if (MSVC)
    target_compile_options(audiocity_rex_sdk PRIVATE /W0 /WX-)
    target_link_libraries(audiocity_rex_sdk PRIVATE Version)
else ()
    target_compile_options(audiocity_rex_sdk PRIVATE -w)
endif ()

target_sources(Audiocity
    PRIVATE
        src/plugin/PluginProcessor.cpp
        src/plugin/PluginEditor.cpp
        src/plugin/PresetJson.cpp
        src/plugin/PlayerPadState.cpp
        src/plugin/CcLearnDial.cpp
        src/engine/EngineCore.cpp
        src/engine/RexLoader.cpp
        src/engine/VoicePool.cpp)

target_include_directories(Audiocity
    PRIVATE
        src)

target_compile_definitions(Audiocity
    PRIVATE
        REX_WINDOWS=1
        REX_MAC=0
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)

target_link_libraries(Audiocity
    PRIVATE
        audiocity_rex_sdk
        juce::juce_audio_utils
        juce::juce_data_structures
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if (MSVC)
    target_compile_options(Audiocity PRIVATE /W4 /WX /permissive-)
endif ()

set(AUDIOCITY_REX_DEPLOY_DLL
    "${CMAKE_SOURCE_DIR}/third_party/REXSDK_Win_1.9.2/Win/x64/Deployment/REX Shared Library.dll")

if (EXISTS "${AUDIOCITY_REX_DEPLOY_DLL}")
    foreach (rex_target Audiocity_Standalone Audiocity_VST3)
        if (TARGET ${rex_target})
            add_custom_command(TARGET ${rex_target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${AUDIOCITY_REX_DEPLOY_DLL}"
                    "$<TARGET_FILE_DIR:${rex_target}>/REX Shared Library.dll")
        endif ()
    endforeach ()
endif ()

include(CTest)
if (BUILD_TESTING)
    add_subdirectory(tests)
endif ()

if (WIN32)
    option(AUDIOCITY_ENABLE_WIX_INSTALLER "Enable WiX MSI installer build target (requires WiX Toolset v3)." OFF)

    if (AUDIOCITY_ENABLE_WIX_INSTALLER)
        set(AUDIOCITY_MSBUILD_EXECUTABLE "")

        if (DEFINED CMAKE_VS_MSBUILD_COMMAND AND NOT CMAKE_VS_MSBUILD_COMMAND STREQUAL "")
            set(AUDIOCITY_MSBUILD_EXECUTABLE "${CMAKE_VS_MSBUILD_COMMAND}")
        else ()
            find_program(AUDIOCITY_MSBUILD_EXECUTABLE NAMES MSBuild.exe msbuild)
        endif ()

        if (AUDIOCITY_MSBUILD_EXECUTABLE)
            add_custom_target(audiocity_wix_installer
                COMMAND "${AUDIOCITY_MSBUILD_EXECUTABLE}"
                    "${CMAKE_SOURCE_DIR}/installer/AudiocityInstaller.wixproj"
                    /t:Build
                    /p:Configuration=$<CONFIG>
                    /p:Platform=x64
                    /p:ProductVersion=${PROJECT_VERSION}
                DEPENDS Audiocity_Standalone Audiocity_VST3
                WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/installer"
                COMMENT "Building Audiocity MSI via WiX Toolset")
        else ()
            message(WARNING "AUDIOCITY_ENABLE_WIX_INSTALLER is ON, but MSBuild was not found. Installer target will not be created.")
        endif ()
    endif ()
endif ()
