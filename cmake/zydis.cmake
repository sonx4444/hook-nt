# Zydis library configuration. Keep the decoder behind ApiScope's internal
# instruction_decoder interface so third-party types do not spread through the
# patcher.
include(FetchContent)

set(ZYDIS_BUILD_SHARED_LIB OFF CACHE BOOL "" FORCE)
set(ZYDIS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ZYDIS_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(ZYDIS_BUILD_DOXYGEN OFF CACHE BOOL "" FORCE)
set(ZYDIS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ZYDIS_FEATURE_ENCODER OFF CACHE BOOL "" FORCE)
set(ZYDIS_FEATURE_FORMATTER OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    zydis
    GIT_REPOSITORY https://github.com/zyantific/zydis.git
    # Zydis v4.1.1.
    GIT_TAG a2278f1d254e492f6a6b39f6cb5d1f5d515659dc
    GIT_SHALLOW TRUE
    GIT_SUBMODULES dependencies/zycore
    GIT_SUBMODULES_RECURSE TRUE
)
FetchContent_MakeAvailable(zydis)

# Third-party warnings are not actionable in ApiScope builds.
if(MSVC)
    target_compile_options(Zydis PRIVATE /w)
    target_compile_options(Zycore PRIVATE /w)
else()
    target_compile_options(Zydis PRIVATE -w)
    target_compile_options(Zycore PRIVATE -w)
endif()
