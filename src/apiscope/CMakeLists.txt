# ApiScope executable CMakeLists.txt
cmake_minimum_required(VERSION 3.20)

# Source files
set(APISCOPE_SOURCES
    main.cpp
    cli_options.cpp
    hook_registry.cpp
    process_manager.cpp
    hook_manager.cpp
    debug_session.cpp
    instruction_decoder.cpp
    trace_renderer.cpp
    trace_session.cpp
    memory_utils.cpp
)

set(APISCOPE_HEADERS
    ../include/process_manager.h
    ../include/hook_registry.h
    ../include/cli_options.h
    ../include/trace_protocol.h
    ../include/trace_ring.h
    ../include/trace_renderer.h
    ../include/trace_session.h
    ../include/hook_manager.h
    ../include/debug_session.h
    ../include/api_hook_descriptor.h
    ../include/instruction_decoder.h
    ../include/memory_utils.h
    ../include/common.h
)

# Create executable
add_executable(apiscope ${APISCOPE_SOURCES} ${APISCOPE_HEADERS})
target_compile_definitions(apiscope PRIVATE
    APISCOPE_VERSION="${PROJECT_VERSION}"
)

# Link libraries
target_link_libraries(apiscope PRIVATE
    ntdll
)

target_link_libraries(apiscope PRIVATE Zydis)

# Include directories
target_include_directories(apiscope PRIVATE
    ../include
)

# Set output directory
set_target_properties(apiscope PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Set working directory for debugging
set_target_properties(apiscope PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
