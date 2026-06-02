# NtdllN DLL CMakeLists.txt
cmake_minimum_required(VERSION 3.20)

file(GLOB NTDLLN_HOOK_SOURCES CONFIGURE_DEPENDS
    hooks/*.cpp
)

# Source files
set(NTDLLN_SOURCES
    trace_transport.cpp
    ../hooknt/memory_utils.cpp
    ${NTDLLN_HOOK_SOURCES}
)

set(NTDLLN_HEADERS
    ../include/trace_protocol.h
    ../include/trace_transport.h
    ../include/nt_hook.h
    ../include/common.h
)

# Create shared library (DLL)
add_library(ntdlln SHARED ${NTDLLN_SOURCES} ${NTDLLN_HEADERS})

# Include directories
target_include_directories(ntdlln PRIVATE
    ../include
)

# Set output directory
set_target_properties(ntdlln PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
)

# Enforce the minimal mapper contract.
if(MSVC)
    target_compile_options(ntdlln PRIVATE /GS- /GR-)
    target_link_options(ntdlln PRIVATE /DEBUG /NOENTRY /NODEFAULTLIB)
endif()

find_program(POWERSHELL_EXECUTABLE powershell REQUIRED)
add_custom_command(TARGET ntdlln POST_BUILD
    COMMAND ${POWERSHELL_EXECUTABLE}
        -NoProfile
        -ExecutionPolicy Bypass
        -File ${CMAKE_SOURCE_DIR}/scripts/validate-ntdlln.ps1
        $<TARGET_FILE:ntdlln>
    VERBATIM
)
