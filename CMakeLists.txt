cmake_minimum_required(VERSION 3.16)

project(mandelSSE
    VERSION 2.11
    DESCRIPTION "Interactive real-time Mandelbrot fractal zoomer (SSE/AVX)"
    LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Compiler requirement
# ---------------------------------------------------------------------------
# The core loops in src/sse.cc use GCC/Clang AT&T-syntax inline assembly
# (asm("...")) and the __builtin_cpu_supports() builtin. MSVC supports
# neither, so only GCC- or Clang-compatible compilers can build this.
if(MSVC)
    message(FATAL_ERROR
        "MSVC is not supported: src/sse.cc uses GNU inline assembly. "
        "Build with MinGW-w64 GCC (e.g. the MSYS2 MINGW64 toolchain) "
        "or Clang in GNU mode.")
endif()

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)  # need GNU extensions (inline asm, builtins)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(ENABLE_OPENMP "Use OpenMP multithreading if available" ON)
option(ENABLE_NATIVE "Tune for the build machine's CPU (-mtune=native)" ON)

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------
find_package(SDL2 REQUIRED)

set(HAVE_OPENMP OFF)
if(ENABLE_OPENMP)
    find_package(OpenMP COMPONENTS CXX)
    if(OpenMP_CXX_FOUND)
        set(HAVE_OPENMP ON)
    else()
        message(WARNING "OpenMP not found; building single-threaded.")
    endif()
endif()

# ---------------------------------------------------------------------------
# Generated headers (replacing autoconf's config.h / version.h)
# ---------------------------------------------------------------------------
# Git revision, mirroring the autoconf SCM_REVISION substitution.
find_package(Git QUIET)
set(SCM_REVISION "unknown")
if(GIT_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/.git")
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse --short HEAD
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        OUTPUT_VARIABLE SCM_REVISION
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
endif()

set(GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
configure_file(src/config.h.cmake.in "${GENERATED_DIR}/config.h" @ONLY)
configure_file(src/version.h.in       "${GENERATED_DIR}/version.h" @ONLY)

# ---------------------------------------------------------------------------
# Target
# ---------------------------------------------------------------------------
add_executable(mandelSSE
    src/mandel.cc
    src/sse.cc
    src/xaos.cc
    src/common.cc)

target_include_directories(mandelSSE PRIVATE
    "${GENERATED_DIR}"   # config.h, version.h
    src)

# SDL2main (static) references symbols from SDL2, so it must be listed BEFORE
# SDL2 on the link line. List it first, then SDL2 itself.
if(TARGET SDL2::SDL2main)
    target_link_libraries(mandelSSE PRIVATE SDL2::SDL2main)
endif()
target_link_libraries(mandelSSE PRIVATE SDL2::SDL2)

if(HAVE_OPENMP)
    target_link_libraries(mandelSSE PRIVATE OpenMP::OpenMP_CXX)
    target_compile_definitions(mandelSSE PRIVATE USE_OPENMP=1)
endif()

# ---------------------------------------------------------------------------
# Optimization flags (mirror of configure.ac's AX_CXXFLAGS_GCC_OPTION set)
# ---------------------------------------------------------------------------
target_compile_options(mandelSSE PRIVATE
    -Wall -Wextra
    $<$<CONFIG:Release>:-O3>
    -fomit-frame-pointer
    -ffast-math
    -funsafe-math-optimizations
    -mrecip
    -mstackrealign)

if(ENABLE_NATIVE)
    target_compile_options(mandelSSE PRIVATE -mtune=native)
endif()

target_compile_definitions(mandelSSE PRIVATE NDEBUG)

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
install(TARGETS mandelSSE RUNTIME DESTINATION bin)

# ---------------------------------------------------------------------------
# Tests (ctest)
# ---------------------------------------------------------------------------
include(CTest)
if(BUILD_TESTING)
    # Differential / throughput test for the SIMD core loops. It links the
    # core loops from src/sse.cc directly (which pulls in SDL.h via common.h,
    # hence the SDL2 include dirs; no SDL functions are actually called).
    add_executable(test_coreloop test/test_coreloop.cc src/sse.cc)
    target_include_directories(test_coreloop PRIVATE "${GENERATED_DIR}" src)
    target_link_libraries(test_coreloop PRIVATE SDL2::SDL2)
    target_compile_options(test_coreloop PRIVATE -O2)
    add_test(NAME coreloop COMMAND test_coreloop)
endif()
