# DiStorm library configuration. Prefer the checked-out submodule, but make a
# fresh clone buildable without requiring a separate submodule command.
set(DISTORM_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/libs/distorm")
if(NOT EXISTS "${DISTORM_ROOT}/src")
    include(FetchContent)
    FetchContent_Declare(
        distorm_source
        GIT_REPOSITORY https://github.com/gdabah/distorm.git
        GIT_TAG 7a02caa1a936f0a653fc75f1aaea9bd3fa654603
        GIT_SHALLOW FALSE
    )
    FetchContent_MakeAvailable(distorm_source)
    set(DISTORM_ROOT "${distorm_source_SOURCE_DIR}")
endif()

file(GLOB_RECURSE DISTORM_SOURCES 
    "${DISTORM_ROOT}/src/*.c"
)

file(GLOB_RECURSE DISTORM_HEADERS
    "${DISTORM_ROOT}/include/*.h"
    "${DISTORM_ROOT}/src/*.h"
)

add_library(distorm STATIC ${DISTORM_SOURCES} ${DISTORM_HEADERS})

# Set include directories
target_include_directories(distorm PUBLIC
    ${DISTORM_ROOT}/include
    ${DISTORM_ROOT}/src
)

# Set compiler definitions for distorm
target_compile_definitions(distorm PRIVATE
    DISTORM_STATIC
    SUPPORT_64BIT_OFFSET
)

# Disable warnings for third-party code
if(MSVC)
    target_compile_options(distorm PRIVATE /w)
else()
    target_compile_options(distorm PRIVATE -w)
endif()

# Set output directory
set_target_properties(distorm PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
)
