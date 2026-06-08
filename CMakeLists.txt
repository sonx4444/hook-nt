cmake_minimum_required(VERSION 3.20)
project(ApiScope VERSION 0.4.0 LANGUAGES C CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set default build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# Compiler-specific options
if(MSVC)
    add_compile_options(/W4)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Configure Zydis
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/zydis.cmake)

add_subdirectory(src/apiscope)
add_subdirectory(src/apiscope-hooks)

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

# Set startup project for Visual Studio
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT apiscope)
