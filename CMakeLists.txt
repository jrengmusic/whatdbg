cmake_minimum_required(VERSION 3.20)
project(whatdbg VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# MSVC settings
if(MSVC)
    add_compile_options(/W4 /EHsc /permissive-)
endif()

add_executable(whatdbg
    src/main.cpp
    src/DapProtocol.cpp
    src/DapDispatcher.cpp
    src/DbgEngSession.cpp
    src/DbgEngCallbacks.cpp
    src/BreakpointManager.cpp
)

target_include_directories(whatdbg PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party
)

target_link_libraries(whatdbg PRIVATE
    dbgeng.lib
    ole32.lib
    oleaut32.lib
)

# ---------------------------------------------------------------------------
# whatdbg_smoke — Phase 2.5 attach smoke test
#
# Standalone binary: attaches to a running DAW by PID, enumerates loaded
# modules, searches for a JUCE plugin DLL, and tests symbol resolution.
#
# Usage: whatdbg_smoke.exe <pid>
# ---------------------------------------------------------------------------

add_executable(whatdbg_smoke
    src/smoke_test.cpp
    src/DbgEngSession.cpp
)

target_include_directories(whatdbg_smoke PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party
)

target_link_libraries(whatdbg_smoke PRIVATE
    dbgeng.lib
    ole32.lib
    oleaut32.lib
)
