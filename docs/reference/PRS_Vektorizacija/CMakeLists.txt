cmake_minimum_required(VERSION 3.16)
project(PerformanceLabs VERSION 1.0 LANGUAGES CXX)


set(CMAKE_SUPPRESS_REGENERATION ON)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(PERFLABS_ENABLE_OPENMP "Enable #pragma omp simd" ON)
option(PERFLABS_ENABLE_AVX2 "Build AVX2" ON)
option(PERFLABS_VECTORIZATION_REPORTS "Compiler vectorization remarks" OFF)


add_executable(lab1_perf_simd
  src/main.cpp
  src/bench.cpp
  src/benchmark_runner.cpp
  src/vectorization_scalar.cpp
  src/vectorization_auto.cpp
  src/vectorization_omp.cpp
  src/vectorization_avx2.cpp
  src/particle_update_scalar.cpp
  src/particle_update_auto.cpp
  src/particle_update_omp.cpp
  src/particle_update_avx2.cpp
)


file(GLOB_RECURSE PERFLABS_HEADERS LIST_DIRECTORIES false
  "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h"
  "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp"
)
if(PERFLABS_HEADERS)
  target_sources(lab1_perf_simd PRIVATE ${PERFLABS_HEADERS})
  source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${PERFLABS_HEADERS})
endif()

target_include_directories(lab1_perf_simd PRIVATE include)

if(MSVC)
  target_compile_options(lab1_perf_simd PRIVATE /W4 /EHsc)
else()
  target_compile_options(lab1_perf_simd PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  set_source_files_properties(src/vectorization_scalar.cpp src/particle_update_scalar.cpp PROPERTIES COMPILE_OPTIONS "-fno-tree-vectorize")
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  set_source_files_properties(src/vectorization_scalar.cpp src/particle_update_scalar.cpp PROPERTIES COMPILE_OPTIONS "-fno-vectorize;-fno-slp-vectorize")
endif()


if(PERFLABS_ENABLE_OPENMP)
  if(MSVC)
    target_compile_options(lab1_perf_simd PRIVATE /openmp:experimental)
    target_compile_definitions(lab1_perf_simd PRIVATE PERFLABS_HAS_OPENMP=1)
  else()
    find_package(OpenMP COMPONENTS CXX QUIET)
    if(OpenMP_CXX_FOUND)
      target_link_libraries(lab1_perf_simd PRIVATE OpenMP::OpenMP_CXX)
      target_compile_definitions(lab1_perf_simd PRIVATE PERFLABS_HAS_OPENMP=1)
    else()
      message(STATUS "OpenMP unavailable: omp-simd variant compiles as ordinary loops")
    endif()
  endif()
endif()

string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" PERF_CPU)
if(PERFLABS_ENABLE_AVX2 AND PERF_CPU MATCHES "^(x86_64|amd64|x64|i[3-6]86)$")
  if(MSVC)
    set_source_files_properties(src/vectorization_avx2.cpp src/particle_update_avx2.cpp PROPERTIES COMPILE_OPTIONS "/arch:AVX2")
  else()
    set_source_files_properties(src/vectorization_avx2.cpp src/particle_update_avx2.cpp PROPERTIES COMPILE_OPTIONS "-mavx2")
  endif()
  target_compile_definitions(lab1_perf_simd PRIVATE PERFLABS_HAS_AVX2_BINARY=1)
else()
  message(STATUS "AVX2 example not built (non-x86 target or disabled)")
endif()

if(PERFLABS_VECTORIZATION_REPORTS)
   if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        set_property(
            SOURCE
                src/vectorization_auto.cpp
                src/vectorization_omp.cpp
                src/particle_update_auto.cpp
            APPEND PROPERTY COMPILE_OPTIONS
                -fopt-info-vec-optimized
                -fopt-info-vec-missed
        )
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        set_property(
            SOURCE
                src/vectorization_auto.cpp
                src/vectorization_omp.cpp
                src/particle_update_auto.cpp
            APPEND PROPERTY COMPILE_OPTIONS
                -Rpass=loop-vectorize
                -Rpass-missed=loop-vectorize
                -Rpass-analysis=loop-vectorize
        )
    endif()
endif()