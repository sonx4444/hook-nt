add_executable(test_file_ops test_file_ops.cpp)
target_link_libraries(test_file_ops PRIVATE
    ntdll
    kernel32
)
set_target_properties(test_file_ops PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
add_test(NAME file_ops COMMAND test_file_ops)

add_executable(test_multithread_file_ops test_multithread_file_ops.cpp)
target_link_libraries(test_multithread_file_ops PRIVATE
    kernel32
)
set_target_properties(test_multithread_file_ops PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
add_test(NAME multithread_file_ops COMMAND test_multithread_file_ops)

add_executable(test_memory_utils
    test_memory_utils.cpp
    ../src/hooknt/memory_utils.cpp
)
target_include_directories(test_memory_utils PRIVATE
    ../src/include
)
set_target_properties(test_memory_utils PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
add_test(NAME memory_utils COMMAND test_memory_utils)

add_executable(test_patch_size
    test_patch_size.cpp
    ../src/hooknt/hook_manager.cpp
    ../src/hooknt/process_manager.cpp
    ../src/hooknt/memory_utils.cpp
)
target_include_directories(test_patch_size PRIVATE
    ../src/include
)
target_link_libraries(test_patch_size PRIVATE
    distorm
    psapi
    ntdll
)
set_target_properties(test_patch_size PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
add_test(NAME patch_size COMMAND test_patch_size)

add_executable(test_hook_registry
    test_hook_registry.cpp
    ../src/hooknt/hook_registry.cpp
)
target_include_directories(test_hook_registry PRIVATE
    ../src/include
)
set_target_properties(test_hook_registry PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
add_test(NAME hook_registry COMMAND test_hook_registry $<TARGET_FILE:ntdlln>)

add_executable(test_cli_options
    test_cli_options.cpp
    ../src/hooknt/cli_options.cpp
    ../src/hooknt/hook_registry.cpp
)
target_include_directories(test_cli_options PRIVATE
    ../src/include
)
set_target_properties(test_cli_options PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
add_test(NAME cli_options COMMAND test_cli_options)

add_executable(test_trace_renderer
    test_trace_renderer.cpp
    ../src/hooknt/trace_renderer.cpp
    ../src/ntdlln/trace_transport.cpp
    ../src/hooknt/memory_utils.cpp
)
target_include_directories(test_trace_renderer PRIVATE
    ../src/include
)
set_target_properties(test_trace_renderer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
add_test(NAME trace_renderer COMMAND test_trace_renderer)

add_executable(test_trace_transport
    test_trace_transport.cpp
    ../src/ntdlln/trace_transport.cpp
    ../src/hooknt/memory_utils.cpp
)
target_include_directories(test_trace_transport PRIVATE
    ../src/include
)
set_target_properties(test_trace_transport PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
add_test(NAME trace_transport COMMAND test_trace_transport)
