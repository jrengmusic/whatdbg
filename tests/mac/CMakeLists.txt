cmake_minimum_required(VERSION 3.25)
project(smoke_liblldb LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(LIBLLDB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../Resources/macos/liblldb")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)

add_executable(smoke_liblldb smoke_liblldb.cpp)
target_include_directories(smoke_liblldb PRIVATE "${LIBLLDB_DIR}/include")
target_link_libraries(smoke_liblldb PRIVATE "${LIBLLDB_DIR}/liblldb.dylib")
set_target_properties(smoke_liblldb PROPERTIES
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "${LIBLLDB_DIR}"
)

set(CODESIGN_IDENTITY "Developer ID Application: Bayu Ardianto (9BDSN9TDX3)")
set(CODESIGN_ENTITLEMENTS "${CMAKE_CURRENT_SOURCE_DIR}/entitlements.plist")

add_custom_command(TARGET smoke_liblldb POST_BUILD
    COMMAND codesign --force --options runtime
            --entitlements "${CODESIGN_ENTITLEMENTS}"
            --sign "${CODESIGN_IDENTITY}"
            "$<TARGET_FILE:smoke_liblldb>"
    COMMENT "Codesigning smoke_liblldb with debugger entitlements"
    VERBATIM)

add_executable(target_program target_program.cpp)
set_target_properties(target_program PROPERTIES
    BUILD_WITH_INSTALL_RPATH TRUE
)

set(TARGET_ENTITLEMENTS "${CMAKE_CURRENT_SOURCE_DIR}/target_entitlements.plist")

add_custom_command(TARGET target_program POST_BUILD
    COMMAND codesign --force
            --entitlements "${TARGET_ENTITLEMENTS}"
            --sign "${CODESIGN_IDENTITY}"
            "$<TARGET_FILE:target_program>"
    COMMENT "Codesigning target_program with get-task-allow"
    VERBATIM)

add_dependencies(smoke_liblldb target_program)

target_compile_definitions(smoke_liblldb PRIVATE
    SMOKE_TARGET_PATH="$<TARGET_FILE:target_program>")

# ============================================================================
# PHASE 5 DIAGNOSTIC PROBES (ephemeral — remove after Phase 5 validation)
# ============================================================================

option(WHATDBG_BUILD_MAC_PROBES "Build ephemeral Phase 5 diagnostic probes" OFF)

if(WHATDBG_BUILD_MAC_PROBES)

    # ------------------------------------------------------------------
    # JUCE discovery (required for fixture_pretty_print only)
    # ------------------------------------------------------------------
    set(JUCE_FOUND_FOR_PROBES FALSE)

    if(DEFINED JUCE_PATH AND EXISTS "${JUCE_PATH}/CMakeLists.txt")
        add_subdirectory("${JUCE_PATH}" "${CMAKE_BINARY_DIR}/JUCE_probes")
        set(JUCE_FOUND_FOR_PROBES TRUE)
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../JUCE/CMakeLists.txt")
        add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../JUCE"
                         "${CMAKE_BINARY_DIR}/JUCE_probes")
        set(JUCE_FOUND_FOR_PROBES TRUE)
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../../JUCE/CMakeLists.txt")
        add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../../JUCE"
                         "${CMAKE_BINARY_DIR}/JUCE_probes")
        set(JUCE_FOUND_FOR_PROBES TRUE)
    elseif(DEFINED ENV{JUCE_PATH} AND EXISTS "$ENV{JUCE_PATH}/CMakeLists.txt")
        add_subdirectory("$ENV{JUCE_PATH}" "${CMAKE_BINARY_DIR}/JUCE_probes")
        set(JUCE_FOUND_FOR_PROBES TRUE)
    endif()

    if(NOT JUCE_FOUND_FOR_PROBES)
        message(FATAL_ERROR
            "WHATDBG_BUILD_MAC_PROBES=ON requires JUCE. "
            "Set -DJUCE_PATH=<path> or place JUCE as a sibling of whatdbg/.")
    endif()

    # ------------------------------------------------------------------
    # fixture_pretty_print — plain executable; debuggee for the probe
    # ------------------------------------------------------------------
    add_executable(fixture_pretty_print
        "${CMAKE_CURRENT_SOURCE_DIR}/fixture_pretty_print.cpp")

    target_link_libraries(fixture_pretty_print PRIVATE juce::juce_core)

    target_compile_options(fixture_pretty_print PRIVATE -g -O0)

    add_custom_command(TARGET fixture_pretty_print POST_BUILD
        COMMAND codesign --force
                --entitlements "${TARGET_ENTITLEMENTS}"
                --sign "${CODESIGN_IDENTITY}"
                "$<TARGET_FILE:fixture_pretty_print>"
        COMMENT "Codesigning fixture_pretty_print with get-task-allow"
        VERBATIM)

    # ------------------------------------------------------------------
    # probe_pretty_print — plain SB API program; links vendored liblldb
    # ------------------------------------------------------------------
    add_executable(probe_pretty_print
        "${CMAKE_CURRENT_SOURCE_DIR}/probe_pretty_print.cpp")

    target_include_directories(probe_pretty_print PRIVATE
        "${LIBLLDB_DIR}/include")

    target_link_libraries(probe_pretty_print PRIVATE
        "${LIBLLDB_DIR}/liblldb.dylib")

    set_target_properties(probe_pretty_print PROPERTIES
        BUILD_WITH_INSTALL_RPATH TRUE
        INSTALL_RPATH            "${LIBLLDB_DIR}"
        CXX_STANDARD             17
        CXX_STANDARD_REQUIRED    ON)

    target_compile_definitions(probe_pretty_print PRIVATE
        PROBE_TARGET_PATH="$<TARGET_FILE:fixture_pretty_print>")

    add_custom_command(TARGET probe_pretty_print POST_BUILD
        COMMAND codesign --force --options runtime
                --entitlements "${CODESIGN_ENTITLEMENTS}"
                --sign "${CODESIGN_IDENTITY}"
                "$<TARGET_FILE:probe_pretty_print>"
        COMMENT "Codesigning probe_pretty_print with debugger entitlements"
        VERBATIM)

    add_dependencies(probe_pretty_print fixture_pretty_print)

    message(STATUS "Phase 5 probes: fixture_pretty_print + probe_pretty_print enabled")

endif() # WHATDBG_BUILD_MAC_PROBES
