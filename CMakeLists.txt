# CMakeLists.txt - whatdbg
# ============================================================================
# DAP adapter for Windows DbgEng / macOS liblldb
# ============================================================================

cmake_minimum_required(VERSION 3.25)

# ============================================================================
# JAM BUILD SYSTEM
# ============================================================================

set(JAM_ROOT "$ENV{HOME}/Documents/Poems/dev/jam")
list(APPEND CMAKE_MODULE_PATH "${JAM_ROOT}/cmake")
include(BuildSetup)

# ============================================================================
# PROJECT
# ============================================================================

project(whatdbg
    VERSION 0.1.0
    LANGUAGES C CXX
    DESCRIPTION "DAP adapter for Windows DbgEng / macOS liblldb"
)

# ============================================================================
# PLATFORM-SPECIFIC BINARY DATA
# ============================================================================

if(WIN32)
    if(CMAKE_SYSTEM_PROCESSOR STREQUAL "ARM64")
        set(BINARY_DATA_ARCH "arm64")
    else()
        set(BINARY_DATA_ARCH "x64")
    endif()

    set(BINARY_DATA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Resources/windows/${BINARY_DATA_ARCH}")

    set(WHATDBG_BINARY_FILES
        "${BINARY_DATA_DIR}/dbgeng.dll"
        "${BINARY_DATA_DIR}/dbghelp.dll"
        "${BINARY_DATA_DIR}/dbgcore.dll"
        "${BINARY_DATA_DIR}/symsrv.dll"
    )

    message(STATUS "BinaryData arch: ${BINARY_DATA_ARCH}")
endif()

if(APPLE)
    # Per-arch build: use CMAKE_OSX_ARCHITECTURES if set to single arch,
    # otherwise detect native architecture from the host.
    if(CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
        set(BINARY_DATA_ARCH "arm64")
    elseif(CMAKE_OSX_ARCHITECTURES STREQUAL "x86_64")
        set(BINARY_DATA_ARCH "x86_64")
    else()
        # Default to native arch (local dev, install.sh)
        execute_process(
            COMMAND uname -m
            OUTPUT_VARIABLE NATIVE_ARCH
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        if(NATIVE_ARCH STREQUAL "arm64")
            set(BINARY_DATA_ARCH "arm64")
        elseif(NATIVE_ARCH STREQUAL "x86_64")
            set(BINARY_DATA_ARCH "x86_64")
        else()
            message(FATAL_ERROR "Unsupported macOS architecture: ${NATIVE_ARCH}")
        endif()
        set(CMAKE_OSX_ARCHITECTURES "${BINARY_DATA_ARCH}" CACHE STRING "" FORCE)
    endif()

    set(BINARY_DATA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Resources/macos/${BINARY_DATA_ARCH}")

    set(WHATDBG_BINARY_FILES
        "${BINARY_DATA_DIR}/liblldb.dylib"
    )

    message(STATUS "BinaryData arch: ${BINARY_DATA_ARCH}")
endif()

# ============================================================================
# PLATFORM SOURCE FILES
# ============================================================================

# Windows-only sources
set(WHATDBG_WIN_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/debug/Session.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/debug/SessionInspection.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/debug/SessionPrettyPrint.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/debug/Loader.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/debug/Loader.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/debug/Callbacks.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/debug/Callbacks.cpp"
)

# macOS-only sources
set(WHATDBG_MAC_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/debug/Session_mac.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/debug/SessionInspection_mac.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/debug/SessionPrettyPrint_mac.cpp"
)

# ============================================================================
# BUILD
# ============================================================================

configure_app(
    TARGET_NAME      whatdbg_App
    PRODUCT_NAME     whatdbg
    VERSION          "${PROJECT_VERSION}"
    MODULES          juce_core
    USER_MODULES     jreng_core
    USER_MODULE_DIR  "${CMAKE_CURRENT_SOURCE_DIR}/modules"
    BINARY_FILES     ${WHATDBG_BINARY_FILES}
    ENTITLEMENTS     "${JAM_ROOT}/cmake/entitlements-debugger.plist"
    PLATFORM_SOURCES_WIN  ${WHATDBG_WIN_SOURCES}
    PLATFORM_SOURCES_MAC  ${WHATDBG_MAC_SOURCES}
)

# ============================================================================
# PLATFORM CONFIGURATION (whatdbg-specific)
# ============================================================================

set(TARGET_NAME "whatdbg_App")

if(WIN32)
    target_link_libraries(${TARGET_NAME} PRIVATE ole32 oleaut32)
    message(STATUS "Platform: Windows (ole32, oleaut32)")
endif()

if(APPLE)
    # SB API headers
    target_include_directories(${TARGET_NAME} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/Resources/macos/include")

    # Link against the per-arch dylib for compile-time symbol resolution.
    # At runtime, DYLD_LIBRARY_PATH (set by the re-exec trampoline in Main.cpp)
    # points to the sidecar extraction dir so dyld finds liblldb.dylib there.
    target_link_libraries(${TARGET_NAME} PRIVATE "${BINARY_DATA_DIR}/liblldb.dylib")

    message(STATUS "Platform: macOS (liblldb embedded as BinaryData, re-exec trampoline)")
endif()

message(STATUS "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━")
message(STATUS "Configuration complete!")
message(STATUS "Generator: ${CMAKE_GENERATOR}")
message(STATUS "Build directory: ${CMAKE_BINARY_DIR}")
message(STATUS "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━")
