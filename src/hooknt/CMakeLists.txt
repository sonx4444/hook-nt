# HookNt executable CMakeLists.txt
cmake_minimum_required(VERSION 3.20)

# Source files
set(HOOKNT_SOURCES
    main.cpp
    hook_registry.cpp
    process_manager.cpp
    hook_manager.cpp
    memory_utils.cpp
)

set(HOOKNT_HEADERS
    ../include/process_manager.h
    ../include/hook_registry.h
    ../include/hook_manager.h
    ../include/memory_utils.h
    ../include/common.h
)

# Create executable
add_executable(hooknt ${HOOKNT_SOURCES} ${HOOKNT_HEADERS})

# Link libraries
target_link_libraries(hooknt PRIVATE
    psapi
    ntdll
)

# Link distorm if available
if(TARGET distorm)
    target_link_libraries(hooknt PRIVATE distorm)
endif()

# Include directories
target_include_directories(hooknt PRIVATE
    ../include
)

# Set output directory
set_target_properties(hooknt PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Set working directory for debugging
set_target_properties(hooknt PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
