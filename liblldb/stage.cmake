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

set(CAST_RESOURCE_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/../Resources/macos")
set(CAST_HEADER_DIRECTORY "${CAST_RESOURCE_DIRECTORY}/include/lldb")

file(MAKE_DIRECTORY "${CAST_RESOURCE_DIRECTORY}/${CAST_HOST_PROCESSOR}" "${CAST_RESOURCE_DIRECTORY}/licenses")
file(COPY_FILE "${CAST_BINARY_DIRECTORY}/lib/liblldb.dylib" "${CAST_LIBLLDB_FILE}")

execute_process(COMMAND install_name_tool -id "@rpath/liblldb.dylib" "${CAST_LIBLLDB_FILE}" COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND strip -x "${CAST_LIBLLDB_FILE}" COMMAND_ERROR_IS_FATAL ANY)

file(GLOB CAST_ROOT_HEADERS "${CAST_BINARY_DIRECTORY}/include/lldb/lldb-*.h")
file(GLOB CAST_API_HEADERS "${CAST_SOURCE_DIRECTORY}/lldb/include/lldb/API/*.h" "${CAST_BINARY_DIRECTORY}/tools/lldb/include/lldb/API/*.h")
file(REMOVE_RECURSE "${CAST_HEADER_DIRECTORY}")
file(COPY ${CAST_ROOT_HEADERS} DESTINATION "${CAST_HEADER_DIRECTORY}")
file(COPY ${CAST_API_HEADERS} DESTINATION "${CAST_HEADER_DIRECTORY}/API")
file(COPY_FILE "${CAST_SOURCE_DIRECTORY}/llvm/LICENSE.TXT" "${CAST_RESOURCE_DIRECTORY}/licenses/LLVM-LICENSE.TXT")
