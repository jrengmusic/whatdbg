#[[*****************************************************************************
                        Codegen Annotated Source of Truth
————————————————————————————————————————————————————————————————————————————————

            ░░████████████░░████████████░░████████████░░████████████
            ░░████  ░░████░░████  ░░████░░████  ░░████    ░░████
            ░░████        ░░████  ░░████░░████            ░░████
            ░░████        ░░████████████░░████████████    ░░████
            ░░████        ░░████  ░░████        ░░████    ░░████
            ░░████  ░░████░░████  ░░████░░████  ░░████    ░░████
            ░░████████████░░████  ░░████░░████████████    ░░████

————————————————————————————————————————————————————————————————————————————————
                         FOR YOUR EYES ONLY, DO NOT EDIT
******************************************************************************]]

cmake_minimum_required(VERSION 3.25)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE INTERNAL "")

cmake_host_system_information(RESULT CAST_TOTAL_MEMORY QUERY TOTAL_PHYSICAL_MEMORY)
cmake_host_system_information(RESULT CAST_LOGICAL_CORES QUERY NUMBER_OF_LOGICAL_CORES)
cmake_host_system_information(RESULT CAST_HOST_PROCESSOR QUERY OS_PLATFORM)
math(EXPR CAST_COMPILE_JOBS "${CAST_TOTAL_MEMORY} / 1280")

if(CAST_COMPILE_JOBS GREATER CAST_LOGICAL_CORES)
    set(CAST_COMPILE_JOBS ${CAST_LOGICAL_CORES})
endif()

set_property(GLOBAL PROPERTY JOB_POOLS compile=${CAST_COMPILE_JOBS})
set(CMAKE_JOB_POOL_COMPILE compile)
set(CMAKE_JOB_POOL_PRECOMPILE_HEADER compile)

option(CAST_SIGN "Run the sign/pkg/notarize/staple chain on a Release build" ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "macOS deployment target")

if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "${CAST_HOST_PROCESSOR}" CACHE STRING "macOS architectures")
elseif(WIN32)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    set(CMAKE_RC_FLAGS "/C65001")
    set(CMAKE_NINJA_CMCLDEPS_RC OFF)
endif()

project(whatdbg
    VERSION "0.1.0"
    LANGUAGES C CXX
    DESCRIPTION "DAP adapter for Windows DbgEng / macOS liblldb"
)

find_program(CAST_CCACHE ccache NO_CACHE REQUIRED)
get_property(CAST_LANGUAGES GLOBAL PROPERTY ENABLED_LANGUAGES)

foreach(CAST_LANGUAGE ${CAST_LANGUAGES})
    set(CMAKE_${CAST_LANGUAGE}_COMPILER_LAUNCHER "${CAST_CCACHE}")
endforeach()

message(STATUS "\nwhatdbg")

execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE CAST_COMMIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

message(STATUS "CAST: configuring ${PROJECT_NAME} ${PROJECT_VERSION}")

set(CMAKE_C_FLAGS_RELEASE "")
set(CMAKE_CXX_FLAGS_RELEASE "")
set(CMAKE_C_FLAGS_DEBUG "")
set(CMAKE_CXX_FLAGS_DEBUG "")

if(APPLE)
    set(CAST_RELEASE_COMPILE_FLAGS
-Wno-shadow;-Wno-unused-parameter;-Wno-float-equal;-Wno-sign-conversion;-Wno-switch-enum;-Wno-implicit-float-conversion;-O3;-flto=thin
    )
    set(CAST_RELEASE_LINK_FLAGS
-dead_strip
    )
    set(CAST_DEBUG_COMPILE_FLAGS
-Wno-shadow;-Wno-unused-parameter;-Wno-float-equal;-Wno-sign-conversion;-Wno-switch-enum;-Wno-implicit-float-conversion;-O0;-g
    )
elseif(WIN32)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        set(CAST_RELEASE_COMPILE_FLAGS
/wd4456;/wd4100;/wd4244;/permissive-;/Zc:rvalueCast;/W4;/FC;/O2;/GL
        )
        set(CAST_RELEASE_LINK_FLAGS
/LTCG;/OPT:REF;/OPT:ICF
        )
        set(CAST_DEBUG_COMPILE_FLAGS
/wd4456;/wd4100;/wd4244;/permissive-;/Zc:rvalueCast;/W4;/FC;/Od;/Zi
        )
    else()
        set(CAST_RELEASE_COMPILE_FLAGS
-Wno-shadow;-Wno-unused-parameter;-Wno-float-equal;-Wno-sign-conversion;-Wno-switch-enum;-Wno-implicit-float-conversion;-O3;-flto=thin
        )
        list(REMOVE_ITEM CAST_RELEASE_COMPILE_FLAGS "-O3")
        list(APPEND CAST_RELEASE_COMPILE_FLAGS "/clang:-O3")
        set(CAST_RELEASE_LINK_FLAGS
/LTCG;/OPT:REF;/OPT:ICF
        )
        set(CAST_DEBUG_COMPILE_FLAGS
-Wno-shadow;-Wno-unused-parameter;-Wno-float-equal;-Wno-sign-conversion;-Wno-switch-enum;-Wno-implicit-float-conversion;-O0;-g
        )
        list(REMOVE_ITEM CAST_DEBUG_COMPILE_FLAGS "-O0" "-g")
        list(APPEND CAST_DEBUG_COMPILE_FLAGS "/Z7")
    endif()
endif()

set(CAST_JUCE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../JUCE")
set(CAST_USER_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../jam")

set(CAST_JUCE_VERSION 8.0.17)

find_package(Git REQUIRED)

set(CAST_JUCE_PATCHES
"${CAST_USER_MODULE_PATH}/patch/juce-cached-image-factory-hook.patch"       # External CachedComponentImage factory
"${CAST_USER_MODULE_PATH}/patch/juce-direct2d-helpers-visibility-hook.patch"# Direct2D helpers visibility gate
"${CAST_USER_MODULE_PATH}/patch/juce-image-subsection-hook.patch"           # Root image and subsection bounds hooks
"${CAST_USER_MODULE_PATH}/patch/juce-vulkan-engine-hook.patch"              # External graphics context factory for Vulkan
"${CAST_USER_MODULE_PATH}/patch/juce-paint-update-rect-hook.patch"          # OS dirty rectangle before native paint; applies after juce-vulkan-engine-hook.patch
"${CAST_USER_MODULE_PATH}/patch/juce-attributed-text-hook.patch"            # Attributed text virtual on LowLevelGraphicsContext; applies after juce-vulkan-engine-hook.patch
"${CAST_USER_MODULE_PATH}/patch/juce-adhoc-sign-hook.patch"                 # No ad-hoc signature on the VST3 bundle
"${CAST_USER_MODULE_PATH}/patch/juce-display-changed-hook.patch"            # Callback after refresh() finds a display change
"${CAST_USER_MODULE_PATH}/patch/juce-plugin-transparency-hook.patch"        # Plugin editor wrappers (VST2, VST3) transparent when the host is END; host plugin windows transparent; applies after juce-vulkan-engine-hook.patch
"${CAST_USER_MODULE_PATH}/patch/juce-key-pressed-hook.patch"                # Host key press callback before a peer's components; applies after juce-plugin-transparency-hook.patch
"${CAST_USER_MODULE_PATH}/patch/juce-plugin-host-type-hook.patch"           # END registered as a known PluginHostType host: HostType END, isEND(), description, host file name detection
"${CAST_USER_MODULE_PATH}/patch/juce-plugin-window-focus-hook.patch"        # VST2 and AU host plugin windows take keyboard focus and make the plugin view first responder; applies after juce-plugin-transparency-hook.patch
"${CAST_USER_MODULE_PATH}/patch/juce-mouse-event-hook.patch"                # Host mouse event callback before a peer's components; applies after juce-key-pressed-hook.patch
"${CAST_USER_MODULE_PATH}/patch/juce-plugin-editor-once-hook.patch"         # VST2 plugin client opens the editor that effEditGetRect created, so the editor is built once; applies after juce-plugin-transparency-hook.patch
"${CAST_USER_MODULE_PATH}/patch/juce-plugin-window-size-hook.patch"         # VST2 host child window takes the plugin's effEditGetRect size in physical pixels on Windows; applies after juce-plugin-transparency-hook.patch
)

if(WIN32)
    set(CAST_JUCE_CACHE_DIR "$ENV{LOCALAPPDATA}/cast/juce")
else()
    set(CAST_JUCE_CACHE_DIR "$ENV{HOME}/.cache/cast/juce")
endif()

set(CAST_JUCE_PATCH_SET_CONCAT "${CAST_JUCE_VERSION}")
foreach(CAST_JUCE_PATCH ${CAST_JUCE_PATCHES})
    file(READ "${CAST_JUCE_PATCH}" CAST_JUCE_PATCH_CONTENT)
    string(APPEND CAST_JUCE_PATCH_SET_CONCAT "${CAST_JUCE_PATCH_CONTENT}")
endforeach()

set(CAST_JUCE_PATCH_SET_CONCAT_FILE "${CMAKE_BINARY_DIR}/cast_juce_patch_set_concat.tmp")
file(WRITE "${CAST_JUCE_PATCH_SET_CONCAT_FILE}" "${CAST_JUCE_PATCH_SET_CONCAT}")
file(SHA256 "${CAST_JUCE_PATCH_SET_CONCAT_FILE}" CAST_JUCE_PATCH_SET_HASH)
file(REMOVE "${CAST_JUCE_PATCH_SET_CONCAT_FILE}")

set(CAST_PATCHED_JUCE_PATH "${CAST_JUCE_CACHE_DIR}/${CAST_JUCE_PATCH_SET_HASH}")
set(CAST_JUCE_PATCH_STAMP_FILE "${CAST_PATCHED_JUCE_PATH}/.patch-stamp")

if(NOT EXISTS "${CAST_JUCE_PATCH_STAMP_FILE}")
    message(STATUS "CAST: patching JUCE into ${CAST_PATCHED_JUCE_PATH}")

    if(EXISTS "${CAST_PATCHED_JUCE_PATH}")
        file(REMOVE_RECURSE "${CAST_PATCHED_JUCE_PATH}")
    endif()

    file(REAL_PATH "${CAST_JUCE_PATH}" CAST_JUCE_PATH)

    file(COPY "${CAST_JUCE_PATH}/"
        DESTINATION "${CAST_PATCHED_JUCE_PATH}"
        PATTERN ".git" EXCLUDE
        PATTERN "docs" EXCLUDE
        PATTERN "DOCS.html" EXCLUDE
    )

    foreach(CAST_JUCE_PATCH ${CAST_JUCE_PATCHES})
        get_filename_component(CAST_JUCE_PATCH_NAME "${CAST_JUCE_PATCH}" NAME)

        execute_process(
            COMMAND "${GIT_EXECUTABLE}" apply --check --ignore-whitespace "${CAST_JUCE_PATCH}"
            WORKING_DIRECTORY "${CAST_PATCHED_JUCE_PATH}"
            RESULT_VARIABLE CAST_JUCE_PATCH_CHECK_RESULT
            ERROR_VARIABLE CAST_JUCE_PATCH_CHECK_ERROR
        )
        if(NOT CAST_JUCE_PATCH_CHECK_RESULT EQUAL 0)
            message(FATAL_ERROR "CAST: JUCE patch failed --check: ${CAST_JUCE_PATCH_NAME}\n${CAST_JUCE_PATCH_CHECK_ERROR}")
        endif()

        execute_process(
            COMMAND "${GIT_EXECUTABLE}" apply --ignore-whitespace "${CAST_JUCE_PATCH}"
            WORKING_DIRECTORY "${CAST_PATCHED_JUCE_PATH}"
            RESULT_VARIABLE CAST_JUCE_PATCH_RESULT
            ERROR_VARIABLE CAST_JUCE_PATCH_ERROR
        )
        if(NOT CAST_JUCE_PATCH_RESULT EQUAL 0)
            message(FATAL_ERROR "CAST: JUCE patch failed to apply: ${CAST_JUCE_PATCH_NAME}\n${CAST_JUCE_PATCH_ERROR}")
        endif()
    endforeach()

    file(WRITE "${CAST_JUCE_PATCH_STAMP_FILE}" "${CAST_JUCE_PATCH_SET_HASH}")
endif()

set(CAST_JUCE_PATH "${CAST_PATCHED_JUCE_PATH}")

set(CAST_JUCE_PATCH_SET_STAMP_FILE "${CMAKE_BINARY_DIR}/cast_juce_patch_set")

if(EXISTS "${CAST_JUCE_PATCH_SET_STAMP_FILE}")
    file(READ "${CAST_JUCE_PATCH_SET_STAMP_FILE}" CAST_BUILT_JUCE_PATCH_SET_HASH)
endif()

if(NOT "${CAST_BUILT_JUCE_PATCH_SET_HASH}" STREQUAL "${CAST_JUCE_PATCH_SET_HASH}")
    file(REMOVE_RECURSE "${CMAKE_BINARY_DIR}/JUCE" "${CMAKE_BINARY_DIR}/vst3_helpers")
    file(WRITE "${CAST_JUCE_PATCH_SET_STAMP_FILE}" "${CAST_JUCE_PATCH_SET_HASH}")
endif()

add_subdirectory("${CAST_JUCE_PATH}" "${CMAKE_BINARY_DIR}/JUCE")

get_directory_property(CAST_FOUND_JUCE_VERSION DIRECTORY "${CAST_JUCE_PATH}" DEFINITION JUCE_VERSION)

if(NOT CAST_FOUND_JUCE_VERSION VERSION_EQUAL CAST_JUCE_VERSION)
    message(FATAL_ERROR "CAST: JUCE ${CAST_FOUND_JUCE_VERSION} found, ${CAST_JUCE_VERSION} required — exact match")
endif()

message(STATUS "CAST: JUCE ${CAST_FOUND_JUCE_VERSION} patched at ${CAST_JUCE_PATH}")

juce_add_module("${CMAKE_CURRENT_SOURCE_DIR}/../jam/jam_core")# JAM Core

message(STATUS "CAST: user modules added from ${CMAKE_CURRENT_SOURCE_DIR}/../jam")

juce_add_console_app(${PROJECT_NAME}
    PRODUCT_NAME "whatdbg"
    VERSION "0.1.0"
    BUILD_VERSION "0.1.0"
    BUNDLE_ID "com.jrengmusic.whatdbg"
    COMPANY_NAME "JRENG"
    COMPANY_COPYRIGHT "© 2026 JRENG"
    COMPANY_WEBSITE "https://jrengmusic.com"
    COMPANY_EMAIL "info@jrengmusic.com"
    DOCUMENT_EXTENSIONS 
    NEEDS_CURL OFF
    NEEDS_WEB_BROWSER OFF
    NEEDS_WEBVIEW2 OFF
    NEEDS_STORE_KIT OFF
)

juce_generate_juce_header(${PROJECT_NAME})

file(GLOB_RECURSE CAST_SOURCE_FILES CONFIGURE_DEPENDS
${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp
${CMAKE_CURRENT_SOURCE_DIR}/Source/*.h
)

target_sources(${PROJECT_NAME} PRIVATE ${CAST_SOURCE_FILES})

target_precompile_headers(${PROJECT_NAME} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:<JuceHeader.h$<ANGLE-R>>")

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(${PROJECT_NAME} PRIVATE "SHELL:-Xclang -fno-pch-timestamp")
endif()

file(GLOB CAST_MODULE_SOURCES
    "${CAST_JUCE_PATH}/modules/*/*.cpp"
    "${CAST_JUCE_PATH}/modules/*/*.mm"
    "${CMAKE_CURRENT_SOURCE_DIR}/../jam/*/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../jam/*/*.mm"
)

set_source_files_properties(${CAST_MODULE_SOURCES} PROPERTIES SKIP_PRECOMPILE_HEADERS ON)

target_compile_definitions(${PROJECT_NAME} PUBLIC
    CAST_COMMIT="${CAST_COMMIT_HASH}"
DONT_SET_USING_JUCE_NAMESPACE=1
JUCE_DONT_DECLARE_PROJECTINFO=1
)

target_include_directories(${PROJECT_NAME} PRIVATE
${CMAKE_CURRENT_SOURCE_DIR}/../jam
${CMAKE_CURRENT_SOURCE_DIR}/Source/generated
"${CMAKE_SOURCE_DIR}/Resources/macos/include"
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
juce_core
jam_core
"$<$<PLATFORM_ID:Darwin>:${CMAKE_SOURCE_DIR}/Resources/macos/${CMAKE_OSX_ARCHITECTURES}/liblldb.dylib>"
"$<$<PLATFORM_ID:Windows>:ole32>"
"$<$<PLATFORM_ID:Windows>:oleaut32>"
    PUBLIC
        juce::juce_recommended_config_flags
)

if(MSVC)
    set_source_files_properties(
        "${CAST_USER_MODULE_PATH}/jam_freetype/jam_freetype.cpp"
        PROPERTIES COMPILE_FLAGS "/wd4005 /wd4804"
    )
else()
    set_source_files_properties(
        "${CAST_USER_MODULE_PATH}/jam_freetype/jam_freetype.cpp"
        "${CAST_USER_MODULE_PATH}/jam_freetype/jam_freetype.mm"
        PROPERTIES COMPILE_FLAGS "-Wno-macro-redefined"
    )
endif()

target_compile_options(${PROJECT_NAME} PRIVATE
    $<$<CONFIG:Release>:${CAST_RELEASE_COMPILE_FLAGS}>
    $<$<CONFIG:Debug>:${CAST_DEBUG_COMPILE_FLAGS}>
)

target_link_options(${PROJECT_NAME} PRIVATE
    $<$<CONFIG:Release>:${CAST_RELEASE_LINK_FLAGS}>
)

file(GLOB CAST_BINARY_FILES CONFIGURE_DEPENDS
"Resources/macos/${CMAKE_OSX_ARCHITECTURES}/liblldb.dylib"
"Resources/windows/${CMAKE_CXX_COMPILER_ARCHITECTURE_ID}/dbgeng.dll"
"Resources/windows/${CMAKE_CXX_COMPILER_ARCHITECTURE_ID}/dbghelp.dll"
"Resources/windows/${CMAKE_CXX_COMPILER_ARCHITECTURE_ID}/dbgcore.dll"
"Resources/windows/${CMAKE_CXX_COMPILER_ARCHITECTURE_ID}/symsrv.dll"
)

juce_add_binary_data(${PROJECT_NAME}_BinaryData
    NAMESPACE "BinaryData"
    SOURCES
        ${CAST_BINARY_FILES}
)

target_link_libraries(${PROJECT_NAME} PRIVATE ${PROJECT_NAME}_BinaryData)

include(CheckIPOSupported)

set(CAST_POST_BUILD_STAMP_FILE "${CMAKE_BINARY_DIR}/post-build.stamp")
set(CAST_POST_BUILD_DEPENDS $<TARGET_FILE:${PROJECT_NAME}>)

if(APPLE)
    set(CAST_PACK_folder "../Release")
elseif(WIN32)
    set(CAST_PACK_folder "../Release")
endif()
if(APPLE)
    set(CAST_PACK_format "pkg")
elseif(WIN32)
    set(CAST_PACK_format "exe")
endif()
if(APPLE)
    set(CAST_PACK_platform "macOS ${CMAKE_OSX_ARCHITECTURES}")
elseif(WIN32)
    set(CAST_PACK_platform "Windows")
endif()

if(WIN32)
    string(TOLOWER "${CMAKE_CXX_COMPILER_ARCHITECTURE_ID}" CAST_PACK_architecture)
    set(CAST_PACK_platform "${CAST_PACK_platform} ${CAST_PACK_architecture}")
endif()

set(CAST_PACK_ARCHIVE "${CMAKE_SOURCE_DIR}/${CAST_PACK_folder}/whatdbg v${PROJECT_VERSION} ${CAST_PACK_platform}.${CAST_PACK_format}")

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    check_ipo_supported(RESULT CAST_IPO_SUPPORTED OUTPUT CAST_IPO_ERROR)

    if(CAST_IPO_SUPPORTED)
        set_target_properties(${PROJECT_NAME} PROPERTIES INTERPROCEDURAL_OPTIMIZATION ON)
        message(STATUS "CAST: LTO enabled")
    else()
        message(WARNING "CAST: LTO not supported — ${CAST_IPO_ERROR}")
    endif()

    if(CAST_SIGN AND APPLE)
        string(TIMESTAMP CAST_BUILD_DATE "%Y-%m-%d" UTC)
        set(CAST_QA_PATH "$ENV{HOME}/Documents/Poems/dev/___builds___")
        set(CAST_QA_DIRECTORY "${CAST_QA_PATH}/${CAST_BUILD_DATE} ${PROJECT_NAME} v${PROJECT_VERSION} ${CAST_PACK_platform}")
        set(CAST_INSTALL_ROOT /opt)
        file(WRITE "${CMAKE_BINARY_DIR}/${PROJECT_NAME}_pkg_scripts/postinstall"
"#!/bin/bash
REAL_USER=$(stat -f \"%Su\" /dev/console)
REAL_HOME=$(eval echo ~$REAL_USER)
mkdir -p \"$REAL_HOME/.local/bin\"
ln -sf \"${CAST_INSTALL_ROOT}/${PROJECT_NAME}/${PROJECT_NAME}\" \"$REAL_HOME/.local/bin/${PROJECT_NAME}\"
")
        file(CHMOD "${CMAKE_BINARY_DIR}/${PROJECT_NAME}_pkg_scripts/postinstall" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
        set(CAST_SIGN_STAMP_FILE "${CMAKE_BINARY_DIR}/sign.stamp")
        string(REPLACE ";" "," CAST_HOST_ARCHITECTURES "${CMAKE_OSX_ARCHITECTURES}")
        configure_file("${CMAKE_SOURCE_DIR}/cast/installer/mac/distribution.xml" "${CMAKE_BINARY_DIR}/installer/distribution.xml" @ONLY)
        add_custom_command(OUTPUT "${CAST_SIGN_STAMP_FILE}"
            DEPENDS $<TARGET_FILE:${PROJECT_NAME}>
COMMAND ${CMAKE_STRIP} $<TARGET_FILE:${PROJECT_NAME}>
COMMAND xattr -cr $<TARGET_FILE:${PROJECT_NAME}>
COMMAND codesign --force --options runtime --entitlements "${CMAKE_SOURCE_DIR}/entitlements.plist" --sign "Developer ID Application: Bayu Ardianto \\(9BDSN9TDX3\\)" $<TARGET_FILE:${PROJECT_NAME}>
COMMAND codesign --verify --verbose $<TARGET_FILE:${PROJECT_NAME}>
COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/${PROJECT_NAME}_pkg_root${CAST_INSTALL_ROOT}/${PROJECT_NAME}"
COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${PROJECT_NAME}> "${CMAKE_BINARY_DIR}/${PROJECT_NAME}_pkg_root${CAST_INSTALL_ROOT}/${PROJECT_NAME}/"
COMMAND pkgbuild --root "${CMAKE_BINARY_DIR}/${PROJECT_NAME}_pkg_root${CAST_INSTALL_ROOT}" --install-location "${CAST_INSTALL_ROOT}" --scripts "${CMAKE_BINARY_DIR}/${PROJECT_NAME}_pkg_scripts" --identifier "com.jrengmusic.whatdbg" --version "${PROJECT_VERSION}" "${CMAKE_BINARY_DIR}/${PROJECT_NAME}.pkg"
COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/installer/resources" "${CMAKE_SOURCE_DIR}/${CAST_PACK_folder}"
COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_SOURCE_DIR}/cast/installer/resources/background.png" "${CMAKE_SOURCE_DIR}/cast/installer/resources/background-dark.png" "${CMAKE_SOURCE_DIR}/LICENSE" "${CMAKE_BINARY_DIR}/installer/resources/"
COMMAND productbuild --distribution "${CMAKE_BINARY_DIR}/installer/distribution.xml" --package-path "${CMAKE_BINARY_DIR}" --resources "${CMAKE_BINARY_DIR}/installer/resources" "${CMAKE_BINARY_DIR}/unsigned.pkg"
COMMAND productsign --sign "Developer ID Installer: Bayu Ardianto \\(9BDSN9TDX3\\)" "${CMAKE_BINARY_DIR}/unsigned.pkg" "${CAST_PACK_ARCHIVE}"
COMMAND xcrun notarytool submit "${CAST_PACK_ARCHIVE}" --keychain-profile "notary" --wait
COMMAND xcrun stapler staple "${CAST_PACK_ARCHIVE}"
COMMAND ${CMAKE_COMMAND} -E make_directory "${CAST_QA_DIRECTORY}"
COMMAND ${CMAKE_COMMAND} -E copy "${CAST_PACK_ARCHIVE}" "${CAST_QA_DIRECTORY}/"
COMMAND ${CMAKE_COMMAND} -E touch "${CAST_SIGN_STAMP_FILE}"
            COMMENT "CAST: sign"
        )
        set(CAST_POST_BUILD_DEPENDS "${CAST_SIGN_STAMP_FILE}")
    elseif(CAST_SIGN AND WIN32)
        cmake_host_system_information(RESULT CAST_INSTALLER_KITS_ROOT QUERY WINDOWS_REGISTRY "HKLM/SOFTWARE/Microsoft/Windows Kits/Installed Roots" VALUE KitsRoot10 VIEW 64_32)
        file(GLOB CAST_INSTALLER_KITS_VERSIONS LIST_DIRECTORIES true "${CAST_INSTALLER_KITS_ROOT}bin/10.*")
        list(SORT CAST_INSTALLER_KITS_VERSIONS COMPARE NATURAL ORDER DESCENDING)
        find_program(CAST_INSTALLER_SIGNTOOL signtool PATHS ${CAST_INSTALLER_KITS_VERSIONS} PATH_SUFFIXES x64 NO_DEFAULT_PATH NO_CACHE REQUIRED)
        file(TO_NATIVE_PATH "${CMAKE_SOURCE_DIR}/cast/installer/resources" CAST_INSTALLER_RESOURCES)
        set(CAST_SIGN_STAMP_FILE "${CMAKE_BINARY_DIR}/sign.stamp")
        add_custom_command(OUTPUT "${CAST_SIGN_STAMP_FILE}"
            DEPENDS $<TARGET_FILE:${PROJECT_NAME}>
COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_SOURCE_DIR}/${CAST_PACK_folder}"
COMMAND makensis "/DARTEFACT=$<SHELL_PATH:$<TARGET_FILE:${PROJECT_NAME}>>" "/DOUTFILE=$<SHELL_PATH:${CAST_PACK_ARCHIVE}>" "/DRESOURCES=${CAST_INSTALLER_RESOURCES}" "/DLICENSE=$<SHELL_PATH:${CMAKE_SOURCE_DIR}/LICENSE>" "/DSIGNTOOL=$<SHELL_PATH:${CAST_INSTALLER_SIGNTOOL}>" "$<SHELL_PATH:${CMAKE_SOURCE_DIR}/cast/installer/win/installer.nsi>"
COMMAND ${CMAKE_COMMAND} -E touch "${CAST_SIGN_STAMP_FILE}"
            COMMENT "CAST: installer"
            VERBATIM
        )
        set(CAST_POST_BUILD_DEPENDS "${CAST_SIGN_STAMP_FILE}")
    endif()
endif()

add_custom_command(OUTPUT "${CAST_POST_BUILD_STAMP_FILE}"
    DEPENDS ${CAST_POST_BUILD_DEPENDS}
COMMAND ${CMAKE_COMMAND} -E make_directory "$ENV{HOME}/.local/bin"
COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${PROJECT_NAME}> "$ENV{HOME}/.local/bin/${PROJECT_NAME}.tmp"
COMMAND ${CMAKE_COMMAND} -E rm -f "$ENV{HOME}/.local/bin/${PROJECT_NAME}.old"
COMMAND ${CMAKE_COMMAND} -E touch "$ENV{HOME}/.local/bin/${PROJECT_NAME}${CMAKE_EXECUTABLE_SUFFIX}"
COMMAND ${CMAKE_COMMAND} -E rename "$ENV{HOME}/.local/bin/${PROJECT_NAME}${CMAKE_EXECUTABLE_SUFFIX}" "$ENV{HOME}/.local/bin/${PROJECT_NAME}.old"
COMMAND ${CMAKE_COMMAND} -E rename "$ENV{HOME}/.local/bin/${PROJECT_NAME}.tmp" "$ENV{HOME}/.local/bin/${PROJECT_NAME}${CMAKE_EXECUTABLE_SUFFIX}"
COMMAND ${CMAKE_COMMAND} -E touch "${CAST_POST_BUILD_STAMP_FILE}"
    COMMENT "CAST: post-build"
)
add_custom_target(post-build ALL DEPENDS "${CAST_POST_BUILD_STAMP_FILE}")

message(STATUS "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━")
message(STATUS "CAST: ${PROJECT_NAME} configured")
message(STATUS "Generator: ${CMAKE_GENERATOR}")
message(STATUS "Build directory: ${CMAKE_BINARY_DIR}")
message(STATUS "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━")
