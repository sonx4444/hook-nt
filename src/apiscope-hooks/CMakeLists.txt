# ApiScope hook DLL CMakeLists.txt
cmake_minimum_required(VERSION 3.20)

file(GLOB APISCOPE_HOOK_SOURCES CONFIGURE_DEPENDS
    hooks/*.cpp
)

# Source files
set(APISCOPE_HOOK_DLL_SOURCES
    trace_transport.cpp
    ../apiscope/memory_utils.cpp
    ${APISCOPE_HOOK_SOURCES}
)

set(APISCOPE_HOOK_DLL_HEADERS
    ../include/trace_protocol.h
    ../include/trace_ring.h
    ../include/trace_transport.h
    ../include/api_hook.h
    ../include/api_hook_descriptor.h
    ../include/common.h
)

# Create shared library (DLL)
add_library(apiscope-hooks SHARED ${APISCOPE_HOOK_DLL_SOURCES} ${APISCOPE_HOOK_DLL_HEADERS})

# Include directories
target_include_directories(apiscope-hooks PRIVATE
    ../include
)

# Set output directory
set_target_properties(apiscope-hooks PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
)

# Enforce the minimal mapper contract.
if(MSVC)
    target_compile_options(apiscope-hooks PRIVATE /GS- /GR-)
    target_link_options(apiscope-hooks PRIVATE /DEBUG /NOENTRY /NODEFAULTLIB)
endif()

find_program(POWERSHELL_EXECUTABLE powershell REQUIRED)
add_custom_command(TARGET apiscope-hooks POST_BUILD
    COMMAND ${POWERSHELL_EXECUTABLE}
        -NoProfile
        -ExecutionPolicy Bypass
        -File ${CMAKE_SOURCE_DIR}/scripts/validate-apiscope-hooks.ps1
        $<TARGET_FILE:apiscope-hooks>
    VERBATIM
)
