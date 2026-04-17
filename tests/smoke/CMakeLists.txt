cmake_minimum_required(VERSION 3.25)
project(whatdbg_smoke LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)

# ----------------------------------------------------------------------------
# JUCE discovery — same search order as root CMakeLists.txt
# ----------------------------------------------------------------------------
set(SMOKE_JUCE_FOUND FALSE)

if(DEFINED JUCE_PATH AND EXISTS "${JUCE_PATH}/CMakeLists.txt")
    add_subdirectory("${JUCE_PATH}" "${CMAKE_BINARY_DIR}/JUCE_smoke")
    set(SMOKE_JUCE_FOUND TRUE)
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../../JUCE/CMakeLists.txt")
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../../JUCE"
                     "${CMAKE_BINARY_DIR}/JUCE_smoke")
    set(SMOKE_JUCE_FOUND TRUE)
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../../../JUCE/CMakeLists.txt")
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../../../JUCE"
                     "${CMAKE_BINARY_DIR}/JUCE_smoke")
    set(SMOKE_JUCE_FOUND TRUE)
elseif(DEFINED ENV{JUCE_PATH} AND EXISTS "$ENV{JUCE_PATH}/CMakeLists.txt")
    add_subdirectory("$ENV{JUCE_PATH}" "${CMAKE_BINARY_DIR}/JUCE_smoke")
    set(SMOKE_JUCE_FOUND TRUE)
endif()

if(NOT SMOKE_JUCE_FOUND)
    message(FATAL_ERROR
        "whatdbg_smoke requires JUCE. Set -DJUCE_PATH=<path> or place JUCE as "
        "a sibling of whatdbg/.")
endif()

# ----------------------------------------------------------------------------
# smoke_fixture — debuggee compiled with -O0 -g, codesigned get-task-allow
# ----------------------------------------------------------------------------
add_executable(smoke_fixture fixture.cpp)
target_link_libraries(smoke_fixture PRIVATE juce::juce_core)
target_compile_options(smoke_fixture PRIVATE -g -O0)

set(CODESIGN_IDENTITY "Developer ID Application: Bayu Ardianto (9BDSN9TDX3)")
set(TARGET_ENTITLEMENTS "${CMAKE_CURRENT_SOURCE_DIR}/../mac/target_entitlements.plist")

add_custom_command(TARGET smoke_fixture POST_BUILD
    COMMAND codesign --force
            --entitlements "${TARGET_ENTITLEMENTS}"
            --sign "${CODESIGN_IDENTITY}"
            "$<TARGET_FILE:smoke_fixture>"
    COMMENT "Codesigning smoke_fixture with get-task-allow"
    VERBATIM)

# ----------------------------------------------------------------------------
# smoke_fixture_wait — long-running target for attach + pause scenarios
# ----------------------------------------------------------------------------
add_executable(smoke_fixture_wait fixture_wait.cpp)
target_compile_options(smoke_fixture_wait PRIVATE -g -O0)

add_custom_command(TARGET smoke_fixture_wait POST_BUILD
    COMMAND codesign --force
            --entitlements "${TARGET_ENTITLEMENTS}"
            --sign "${CODESIGN_IDENTITY}"
            "$<TARGET_FILE:smoke_fixture_wait>"
    COMMENT "Codesigning smoke_fixture_wait with get-task-allow"
    VERBATIM)

# ----------------------------------------------------------------------------
# smoke_fixture_crash — deliberate-crash target for exception-surfacing scenario
# ----------------------------------------------------------------------------
add_executable(smoke_fixture_crash fixture_crash.cpp)
target_compile_options(smoke_fixture_crash PRIVATE -g -O0)

add_custom_command(TARGET smoke_fixture_crash POST_BUILD
    COMMAND codesign --force
            --entitlements "${TARGET_ENTITLEMENTS}"
            --sign "${CODESIGN_IDENTITY}"
            "$<TARGET_FILE:smoke_fixture_crash>"
    COMMENT "Codesigning smoke_fixture_crash with get-task-allow"
    VERBATIM)
