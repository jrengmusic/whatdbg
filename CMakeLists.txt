# CMakeLists.txt - whatdbg
# ============================================================================
# DAP adapter for Windows DbgEng
#
# Usage:
#   mkdir -p Builds/Ninja && cd Builds/Ninja && cmake -G Ninja ../..
#
# Directory Structure:
#   dev/
#   ├── whatdbg/
#   │   ├── CMakeLists.txt      <- This file
#   │   ├── Source/             <- Your .cpp/.h files
#   │   └── Resources/windows/{x64,arm64}/  <- DbgEng DLLs (BinaryData)
#   └── JUCE/                   <- JUCE framework (sibling)
# ============================================================================

cmake_minimum_required(VERSION 3.25)


# ============================================================================
# CONFIGURATION
# ============================================================================

set(PROJECT_NAME "whatdbg")
set(PROJECT_VERSION "0.1.0")
set(PROJECT_COMPANY "JRENG")

set(TARGET_TYPE "Console")
set(TARGET_NAME "${PROJECT_NAME}_App")

set(JUCE_MODULES
    juce_core
)

set(USER_MODULES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/modules")

set(USER_MODULES
    jreng_core
)

set(SOURCE_MODE "AUTO_GLOB")
set(SOURCE_FILES)

if(WIN32)
    if(CMAKE_SYSTEM_PROCESSOR STREQUAL "ARM64")
        set(BINARY_DATA_ARCH "arm64")
    else()
        set(BINARY_DATA_ARCH "x64")
    endif()

    set(BINARY_DATA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Resources/windows/${BINARY_DATA_ARCH}")

    set(BINARY_DATA_FILES
        "${BINARY_DATA_DIR}/dbgeng.dll"
        "${BINARY_DATA_DIR}/dbghelp.dll"
        "${BINARY_DATA_DIR}/dbgcore.dll"
        "${BINARY_DATA_DIR}/symsrv.dll"
    )

    message(STATUS "BinaryData arch: ${BINARY_DATA_ARCH}")
endif()

# ============================================================================
# JUCE MODULE SOURCE GROUPS
# ============================================================================
set(JUCE_ENABLE_MODULE_SOURCE_GROUPS ON CACHE BOOL "Show JUCE module sources in IDE")

# ============================================================================
# JUCE FRAMEWORK DISCOVERY
# ============================================================================

set(JUCE_FOUND FALSE)

if(DEFINED JUCE_PATH AND EXISTS "${JUCE_PATH}/CMakeLists.txt")
    message(STATUS "JUCE found: ${JUCE_PATH} (CMake variable)")
    add_subdirectory("${JUCE_PATH}" "${CMAKE_BINARY_DIR}/JUCE")
    set(JUCE_FOUND TRUE)

elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../JUCE/CMakeLists.txt")
    set(JUCE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../JUCE")
    message(STATUS "JUCE found: ${JUCE_PATH} (sibling directory)")
    add_subdirectory("${JUCE_PATH}" "${CMAKE_BINARY_DIR}/JUCE")
    set(JUCE_FOUND TRUE)

elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../JUCE/CMakeLists.txt")
    set(JUCE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../JUCE")
    message(STATUS "JUCE found: ${JUCE_PATH} (cousin directory)")
    add_subdirectory("${JUCE_PATH}" "${CMAKE_BINARY_DIR}/JUCE")
    set(JUCE_FOUND TRUE)

elseif(DEFINED ENV{JUCE_PATH} AND EXISTS "$ENV{JUCE_PATH}/CMakeLists.txt")
    set(JUCE_PATH "$ENV{JUCE_PATH}")
    message(STATUS "JUCE found: ${JUCE_PATH} (environment variable)")
    add_subdirectory("${JUCE_PATH}" "${CMAKE_BINARY_DIR}/JUCE")
    set(JUCE_FOUND TRUE)
endif()

if(NOT JUCE_FOUND)
    message(STATUS "JUCE not found locally — fetching v8.0.12 via FetchContent")
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.12
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(JUCE)
    set(JUCE_FOUND TRUE)
endif()

message(STATUS "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━")

# ============================================================================
# PROJECT DECLARATION
# ============================================================================

project("${PROJECT_NAME}"
    VERSION ${PROJECT_VERSION}
    LANGUAGES C CXX
    DESCRIPTION "DAP adapter for Windows DbgEng"
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

message(STATUS "Project: ${PROJECT_NAME} v${PROJECT_VERSION}")
message(STATUS "Target type: ${TARGET_TYPE}")
message(STATUS "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━")

# ============================================================================
# SOURCE FILE ORGANIZATION
# ============================================================================

if(SOURCE_MODE STREQUAL "AUTO_GLOB")
    file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cc"
        "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.hpp"
    )

    message(STATUS "Source files (auto-discovered):")
    foreach(SRC ${SOURCES})
        file(RELATIVE_PATH REL_PATH "${CMAKE_CURRENT_SOURCE_DIR}" "${SRC}")
        message(STATUS "  ${REL_PATH}")
    endforeach()

else()
    set(SOURCES)
    foreach(SRC ${SOURCE_FILES})
        list(APPEND SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/${SRC}")
    endforeach()

    message(STATUS "Source files (explicit):")
    foreach(SRC ${SOURCE_FILES})
        message(STATUS "  ${SRC}")
    endforeach()
endif()

if(NOT SOURCES)
    message(FATAL_ERROR "No source files found!")
endif()

message(STATUS "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━")

# ============================================================================
# BINARY DATA
# ============================================================================

if(BINARY_DATA_FILES)
    message(STATUS "Binary data files:")
    foreach(FILE ${BINARY_DATA_FILES})
        file(RELATIVE_PATH REL "${CMAKE_CURRENT_SOURCE_DIR}" "${FILE}")
        message(STATUS "  ${REL}")
    endforeach()
else()
    message(STATUS "Binary data: none")
endif()

message(STATUS "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━")

# ============================================================================
# TARGET CREATION
# ============================================================================

juce_add_console_app(${TARGET_NAME}
    PRODUCT_NAME "${PROJECT_NAME}"
    VERSION "${PROJECT_VERSION}"
    COMPANY_NAME "${PROJECT_COMPANY}"
)

set_target_properties(${TARGET_NAME} PROPERTIES OUTPUT_NAME "${PROJECT_NAME}")

target_sources(${TARGET_NAME} PRIVATE ${SOURCES})

target_include_directories(${TARGET_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Source"
)

message(STATUS "Target created: ${TARGET_NAME}")
message(STATUS "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━")

# ============================================================================
# SOURCE GROUPS FOR IDE
# ============================================================================

foreach(ABS_SRC ${SOURCES})
    file(RELATIVE_PATH REL_PATH "${CMAKE_CURRENT_SOURCE_DIR}" "${ABS_SRC}")
    get_filename_component(REL_DIR "${REL_PATH}" DIRECTORY)

    if(NOT REL_DIR STREQUAL "")
        source_group("${REL_DIR}" FILES "${ABS_SRC}")
    else()
        source_group("Source Files" FILES "${ABS_SRC}")
    endif()
endforeach()

# ============================================================================
# JUCEHEADER PRE-GENERATION
# ============================================================================

function(_whatdbg_pre_generate_juce_header target)
    get_target_property(juce_library_code ${target} JUCE_GENERATED_SOURCES_DIRECTORY)
    if(NOT juce_library_code)
        return()
    endif()

    file(MAKE_DIRECTORY "${juce_library_code}")

    if(NOT TARGET juce::juceaide)
        message(WARNING "juce::juceaide not found")
        return()
    endif()

    get_target_property(juceaide_exe juce::juceaide IMPORTED_LOCATION)

    if(NOT juceaide_exe)
        foreach(config Release Debug RelWithDebInfo MinSizeRel)
            get_target_property(juceaide_exe juce::juceaide IMPORTED_LOCATION_${config})
            if(juceaide_exe)
                break()
            endif()
        endforeach()
    endif()

    if(NOT juceaide_exe)
        set(juceaide_exe $<TARGET_FILE:juce::juceaide>)
    endif()

    get_target_property(compile_defs ${target} COMPILE_DEFINITIONS)
    set(module_defs "")
    if(compile_defs)
        foreach(def ${compile_defs})
            if(def MATCHES "^JUCE_MODULE_AVAILABLE_" OR def MATCHES "^DONT_SET_USING_JUCE_NAMESPACE")
                list(APPEND module_defs "${def}")
            endif()
        endforeach()
    endif()

    string(ASCII 30 RS)
    string(ASCII 31 US)

    set(defs_file "${juce_library_code}/Defs.txt")
    set(defs_content "MODULE_DEFINITIONS${US}")
    foreach(def ${module_defs})
        string(APPEND defs_content "${def};")
    endforeach()
    string(APPEND defs_content "${RS}")
    file(WRITE "${defs_file}" "${defs_content}")

    set(juce_header "${juce_library_code}/JuceHeader.h")
    if(NOT EXISTS "${juce_header}")
        execute_process(
            COMMAND "${juceaide_exe}" header "${defs_file}" "${juce_header}"
            RESULT_VARIABLE result
            ERROR_VARIABLE error
            OUTPUT_QUIET
        )

        if(result EQUAL 0)
            message(STATUS "Pre-generated JuceHeader.h")
        else()
            message(WARNING "Failed to pre-generate JuceHeader.h: ${error}")
        endif()
    endif()
endfunction()

_whatdbg_pre_generate_juce_header(${TARGET_NAME})
juce_generate_juce_header(${TARGET_NAME})

message(STATUS "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━")

# ============================================================================
# BINARY DATA TARGET
# ============================================================================

if(BINARY_DATA_FILES)
    juce_add_binary_data(${TARGET_NAME}_BinaryData
        NAMESPACE "BinaryData"
        SOURCES ${BINARY_DATA_FILES}
    )

    target_link_libraries(${TARGET_NAME} PRIVATE ${TARGET_NAME}_BinaryData)
    message(STATUS "BinaryData target created")
endif()

message(STATUS "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━")

# ============================================================================
# MODULE LINKING
# ============================================================================

message(STATUS "Linking JUCE modules:")

foreach(MODULE ${JUCE_MODULES})
    if(TARGET ${MODULE})
        target_link_libraries(${TARGET_NAME} PRIVATE ${MODULE})
        message(STATUS "  ${MODULE}")
    else()
        message(FATAL_ERROR "JUCE module not found: ${MODULE}")
    endif()
endforeach()

# User modules
foreach(MODULE_NAME ${USER_MODULES})
    set(MODULE_DIR "${USER_MODULES_DIR}/${MODULE_NAME}")

    if(EXISTS "${MODULE_DIR}")
        juce_add_module("${MODULE_DIR}")
        target_link_libraries(${TARGET_NAME} PRIVATE ${MODULE_NAME})
        message(STATUS "  ${MODULE_NAME} (user)")
    else()
        message(FATAL_ERROR "User module not found: ${MODULE_NAME}")
    endif()
endforeach()

target_compile_definitions(${TARGET_NAME} PRIVATE
    DONT_SET_USING_JUCE_NAMESPACE=1
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:${TARGET_NAME},JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:${TARGET_NAME},JUCE_VERSION>"
)

if(COMMAND _juce_fixup_module_source_groups)
    _juce_fixup_module_source_groups()
endif()

message(STATUS "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━")

# ============================================================================
# PLATFORM CONFIGURATION
# ============================================================================

if(WIN32)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_options(${TARGET_NAME} PRIVATE /permissive-)
    target_link_libraries(${TARGET_NAME} PRIVATE ole32 oleaut32)
    message(STATUS "Platform: Windows")
endif()

message(STATUS "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━")

# ============================================================================
# CLANGD CONFIGURATION (Auto-generated for LSP)
# ============================================================================

set(CLANGD_CONFIG "${CMAKE_CURRENT_SOURCE_DIR}/.clangd")
set(CLANGD_CONTENT "CompileFlags:\n  CompilationDatabase: ${CMAKE_CURRENT_SOURCE_DIR}/Builds/Ninja/\n  Add:\n")

if(WIN32)
    string(APPEND CLANGD_CONTENT "    - -D_WIN32=1\n")
    string(APPEND CLANGD_CONTENT "    - -DJUCE_WINDOWS=1\n")
    string(APPEND CLANGD_CONTENT "    - -DJUCE_MAC=0\n")
    string(APPEND CLANGD_CONTENT "    - -DJUCE_IOS=0\n")
    string(APPEND CLANGD_CONTENT "    - -DJUCE_LINUX=0\n")
endif()

string(APPEND CLANGD_CONTENT "Diagnostics:\n  MissingIncludes: None\n  UnusedIncludes: None\n")

file(WRITE "${CLANGD_CONFIG}" "${CLANGD_CONTENT}")
message(STATUS "Generated .clangd for LSP")

message(STATUS "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━")
message(STATUS "Configuration complete!")
message(STATUS "Generator: ${CMAKE_GENERATOR}")
message(STATUS "Build directory: ${CMAKE_BINARY_DIR}")
message(STATUS "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━")
