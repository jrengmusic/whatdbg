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

project(liblldb LANGUAGES NONE)

include(ExternalProject)

cmake_host_system_information(RESULT CAST_HOST_PROCESSOR QUERY OS_PLATFORM)

set(CAST_LIBLLDB_FILE "${CMAKE_SOURCE_DIR}/../Resources/macos/${CAST_HOST_PROCESSOR}/liblldb.dylib")

ExternalProject_Add(llvm
    PREFIX llvmorg-23.1.6
    URL "https://github.com/llvm/llvm-project/archive/refs/tags/llvmorg-23.1.6.tar.gz"
    SOURCE_SUBDIR llvm
    CMAKE_CACHE_ARGS
        -DCMAKE_BUILD_TYPE:STRING=MinSizeRel
        -DCMAKE_OSX_ARCHITECTURES:STRING=${CAST_HOST_PROCESSOR}
        -DCMAKE_OSX_DEPLOYMENT_TARGET:STRING=12.0
        -DLLVM_ENABLE_PROJECTS:STRING=clang;lldb
        -DLLVM_ENABLE_ZSTD:BOOL=OFF
        -DLLDB_ENABLE_LZMA:BOOL=OFF
        -DLLVM_TARGETS_TO_BUILD:STRING=Native
        -DLLDB_ENABLE_PYTHON:BOOL=OFF
        -DLLDB_ENABLE_LUA:BOOL=OFF
        -DLLDB_ENABLE_DYNAMIC_SCRIPTINTERPRETERS:BOOL=OFF
        -DLLDB_ENABLE_PROTOCOL_SERVERS:BOOL=OFF
        -DLLDB_ENABLE_GITHUB_BUG_REPORTER:BOOL=OFF
        -DLLDB_ENABLE_LIBEDIT:BOOL=OFF
        -DLLDB_ENABLE_CURSES:BOOL=OFF
        -DLLDB_ENABLE_LIBXML2:BOOL=OFF
        -DLLDB_ENABLE_TREESITTER:BOOL=OFF
        -DLLDB_INCLUDE_TESTS:BOOL=OFF
        -DLLDB_USE_SYSTEM_DEBUGSERVER:BOOL=ON
    BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --target liblldb
    INSTALL_COMMAND ""
    USES_TERMINAL_DOWNLOAD ON
    USES_TERMINAL_CONFIGURE ON
    USES_TERMINAL_BUILD ON
)

ExternalProject_Add_Step(llvm stage
    COMMAND ${CMAKE_COMMAND} -DCAST_SOURCE_DIRECTORY=<SOURCE_DIR> -DCAST_BINARY_DIRECTORY=<BINARY_DIR> -DCAST_HOST_PROCESSOR=${CAST_HOST_PROCESSOR} "-DCAST_LIBLLDB_FILE=${CAST_LIBLLDB_FILE}" -P "${CMAKE_SOURCE_DIR}/stage.cmake"
    DEPENDEES build
    DEPENDS "${CMAKE_SOURCE_DIR}/stage.cmake"
    BYPRODUCTS "${CAST_LIBLLDB_FILE}"
    USES_TERMINAL ON
)
