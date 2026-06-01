# NtdllN DLL CMakeLists.txt
cmake_minimum_required(VERSION 3.20)

file(GLOB NTDLLN_HOOK_SOURCES CONFIGURE_DEPENDS
    hooks/*.cpp
)

# Source files
set(NTDLLN_SOURCES
    dllmain.cpp
    module_resolver.cpp
    function_resolver.cpp
    logger.cpp
    ../hooknt/memory_utils.cpp
    ${NTDLLN_HOOK_SOURCES}
)

set(NTDLLN_HEADERS
    ../include/module_resolver.h
    ../include/function_resolver.h
    ../include/logger.h
    ../include/nt_hook.h
    ../include/common.h
)

# Create shared library (DLL)
add_library(ntdlln SHARED ${NTDLLN_SOURCES} ${NTDLLN_HEADERS})

# Link libraries
target_link_libraries(ntdlln PRIVATE
    ntdll
    kernel32
)

# Include directories
target_include_directories(ntdlln PRIVATE
    ../include
)

# Set DLL export definitions
target_compile_definitions(ntdlln PRIVATE
    NTDLLN_EXPORTS
)

# Set output directory
set_target_properties(ntdlln PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
)

# Generate PDB files for debugging
if(MSVC)
    set_target_properties(ntdlln PROPERTIES
        LINK_FLAGS "/DEBUG"
    )
endif()
