# PS5 application integration; upstream desktop builds do not include this file.
include(FetchContent)

# libc++ 18 ships stop_token/jthread behind its experimental-library switch.
# Aurora uses these for cancellable workers; their implementation is header-only.
add_compile_definitions(_LIBCPP_ENABLE_EXPERIMENTAL)
add_compile_options(-ffunction-sections -fdata-sections -fno-omit-frame-pointer)

# Reuse the separately cross-built renderer dependency during game compilation.
# This avoids rebuilding its shader compiler in every game build directory. The
# direct Vulkan renderer test remains independent of this fallback dependency.
set(PS5_DAWN_BUILD_DIR "${CMAKE_SOURCE_DIR}/build/dawn-ps5" CACHE PATH "Cross-built Dawn directory")
set(PS5_DAWN_SOURCE_DIR "${CMAKE_SOURCE_DIR}/.deps/src/dawn" CACHE PATH "Pinned Dawn source directory")
if (EXISTS "${PS5_DAWN_BUILD_DIR}/gen/include/dawn/webgpu_cpp.h")
    add_library(webgpu_dawn STATIC IMPORTED GLOBAL)
    set_target_properties(webgpu_dawn PROPERTIES
        IMPORTED_LOCATION "${PS5_DAWN_BUILD_DIR}/src/dawn/native/libwebgpu_dawn.a"
        INTERFACE_INCLUDE_DIRECTORIES "${PS5_DAWN_SOURCE_DIR}/include;${PS5_DAWN_BUILD_DIR}/gen/include"
    )
    add_library(dawn::webgpu_dawn ALIAS webgpu_dawn)
    add_library(ps5_dawn_headers INTERFACE)
    add_library(dawn::dawncpp_headers ALIAS ps5_dawn_headers)
    target_include_directories(ps5_dawn_headers INTERFACE
        "${PS5_DAWN_SOURCE_DIR}/include" "${PS5_DAWN_BUILD_DIR}/gen/include")
endif ()

# Disabled Tracy instrumentation only needs headers. Its normal FreeBSD build
# links execinfo, which is not part of the PS5 SDK. Keep profiling explicitly off.
set(TRACY_ENABLE OFF CACHE BOOL "Enable Tracy profiling" FORCE)
FetchContent_Declare(tracy
    URL https://github.com/wolfpld/tracy/archive/refs/tags/v0.14.1.zip
    URL_HASH SHA256=908f3a2917fa86a247abfcf85dcf04bad1db6986a4d40f94b70512f3e9e98d5b
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE
    SOURCE_SUBDIR ps5-headers-only
)
FetchContent_MakeAvailable(tracy)
add_library(ps5_tracy_headers INTERFACE)
add_library(Tracy::TracyClient ALIAS ps5_tracy_headers)
add_library(TracyClient ALIAS ps5_tracy_headers)
target_include_directories(ps5_tracy_headers SYSTEM INTERFACE "${tracy_SOURCE_DIR}/public")

# Native homebrew updates are deployed externally. Desktop Discord IPC and
# crash handlers are not available in a PS5 title.
set(BOREALIS_ENABLE_DISCORD OFF CACHE BOOL "Build Discord integration" FORCE)
set(BOREALIS_ENABLE_SENTRY OFF CACHE BOOL "Build Sentry integration" FORCE)
set(BOREALIS_ENABLE_CRASH OFF CACHE BOOL "Build desktop crash handlers" FORCE)
add_library(ps5_crash STATIC "${CMAKE_CURRENT_LIST_DIR}/crash.cpp")
add_library(borealis::crash ALIAS ps5_crash)

# The initial native title accepts raw GameCube ISO. Retain nod's public header
# for the existing Aurora/Borealis API, with a bounded C++ raw-disc backend.
FetchContent_Declare(aurora_nod
    GIT_REPOSITORY https://github.com/encounter/nod.git
    GIT_TAG ebd80cac99b48a323200d84365e07a58cc27412d
    SOURCE_SUBDIR ps5-header-only
)
FetchContent_MakeAvailable(aurora_nod)
add_library(ps5_raw_disc STATIC "${CMAKE_CURRENT_LIST_DIR}/disc/raw_gamecube.cpp")
target_include_directories(ps5_raw_disc PUBLIC "${aurora_nod_SOURCE_DIR}/nod-ffi/include")
add_library(nod::nod ALIAS ps5_raw_disc)
add_library(nod::nod_static ALIAS ps5_raw_disc)
