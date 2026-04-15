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
