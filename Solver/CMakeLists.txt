cmake_minimum_required(VERSION 3.20)
project(SmokeSolver VERSION 0.1.0 LANGUAGES CXX)

# Solver is a standalone project: no OpenGL, no GLFW, no GLM, no Renderer headers.
# The solver library itself has no third-party dependencies. The tests use doctest, vendored as a
# single header in third_party/doctest (decision D5): nothing is downloaded at configure time.
# See docs/plan/ARCHITECTURE.md.

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Measurements are meaningless without optimisation, so Release is the default.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(SMOKE_BUILD_CLI   "Build the console program smoke_cli" ON)
option(SMOKE_BUILD_TESTS "Build the tests (smoke_tests)"       ON)
option(SMOKE_BUILD_BENCH "Build the benchmark (smoke_bench)"   ON)
option(SMOKE_BUILD_LAB   "Build smoke_lab (side-by-side kernel versions)" ON)
option(SMOKE_DISABLE_VECTORIZATION "Build the B0 scalar baseline" OFF)

# Reference build rules (docs/plan/ARCHITECTURE.md, section 5):
#   - no -ffast-math  (changes results)
#   - no -march=native (teammates have different CPUs; SIMD flags are added per file later)
function(smoke_set_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /EHsc)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endfunction()

add_library(smoke_solver STATIC
  src/SmokeSolver.cpp
  src/Field3D.cpp
  src/DiagnosticsCompute.cpp
  src/stages/Sources.cpp
  src/stages/Forces.cpp
  src/stages/Advection.cpp
  src/stages/Projection.cpp
  src/stages/Boundary.cpp
  src/CpuFeatures.cpp
)

# AVX2 kernels (lab techniques T15/T16, docs/plan/OPTIMIZATION.md): only the *_avx2.cpp files get the AVX2
# flag, so the program still runs on any x86-64 CPU; the choice is made at run time (CpuFeatures.h).
# B0 (SMOKE_DISABLE_VECTORIZATION) keeps them out, so B0 stays "no vectorization".
option(SMOKE_ENABLE_AVX2 "Build AVX2 kernels with a run-time CPU check (x86 only)" ON)
string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" SMOKE_CPU)
if(SMOKE_ENABLE_AVX2 AND NOT SMOKE_DISABLE_VECTORIZATION AND SMOKE_CPU MATCHES "^(x86_64|amd64|x64|i[3-6]86)$")
  set(SMOKE_AVX2_SOURCES src/stages/Projection_avx2.cpp src/stages/Advection_avx2.cpp src/stages/Forces_avx2.cpp)
  target_sources(smoke_solver PRIVATE ${SMOKE_AVX2_SOURCES})
  if(MSVC)
    set_source_files_properties(${SMOKE_AVX2_SOURCES} PROPERTIES COMPILE_OPTIONS "/arch:AVX2")
  else()
    set_source_files_properties(${SMOKE_AVX2_SOURCES} PROPERTIES COMPILE_OPTIONS "-mavx2")
  endif()
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # M4-017: g++'s cost model picks 4-float vectors for the fused advection rows (advectRow2/3) even with
    # -mavx2; "unlimited" lets it use 8 floats. Same arithmetic, so the result does not change.
    set_property(SOURCE src/stages/Advection_avx2.cpp APPEND PROPERTY COMPILE_OPTIONS "-fvect-cost-model=unlimited")
  endif()
  target_compile_definitions(smoke_solver PRIVATE SMOKE_HAS_AVX2_BINARY=1)
  set(SMOKE_AVX2_STATE "AVX2 kernels built (run-time check)")
else()
  set(SMOKE_AVX2_STATE "no AVX2 kernels")
endif()
message(STATUS "smoke_solver: ${SMOKE_AVX2_STATE}")

# M4-018: without -fno-math-errno, std::sqrt in a loop keeps a library call (to set errno for negative input),
# so the vorticity confinement loops cannot be vectorized. The flag does NOT change any result: sqrt of a
# non-negative number is exact in IEEE 754, and it is not part of -ffast-math's value-changing options.
# Only the force kernels get it.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  set_property(SOURCE src/stages/Forces.cpp src/stages/Forces_avx2.cpp APPEND PROPERTY COMPILE_OPTIONS "-fno-math-errno")
endif()

# Text for "smoke_bench --hwinfo": the per-file options above, read back from the source properties so the
# printed text cannot drift from what is really used.
set(SMOKE_PER_FILE_FLAGS "")
foreach(SMOKE_FLAGGED_SOURCE src/stages/Projection_avx2.cpp src/stages/Advection_avx2.cpp
                             src/stages/Forces_avx2.cpp src/stages/Forces.cpp)
  get_source_file_property(SMOKE_SOURCE_FLAGS ${SMOKE_FLAGGED_SOURCE} COMPILE_OPTIONS)
  list(FIND SMOKE_AVX2_SOURCES ${SMOKE_FLAGGED_SOURCE} SMOKE_SOURCE_IS_AVX2)
  if(SMOKE_SOURCE_FLAGS AND (SMOKE_SOURCE_IS_AVX2 GREATER -1 OR SMOKE_FLAGGED_SOURCE STREQUAL "src/stages/Forces.cpp"))
    get_filename_component(SMOKE_SOURCE_NAME ${SMOKE_FLAGGED_SOURCE} NAME)
    string(REPLACE ";" " " SMOKE_SOURCE_FLAGS "${SMOKE_SOURCE_FLAGS}")
    if(SMOKE_PER_FILE_FLAGS)
      string(APPEND SMOKE_PER_FILE_FLAGS ", ")   # not ";": that is the CMake list separator
    endif()
    string(APPEND SMOKE_PER_FILE_FLAGS "${SMOKE_SOURCE_NAME}: ${SMOKE_SOURCE_FLAGS}")
  endif()
endforeach()
if(NOT SMOKE_PER_FILE_FLAGS)
  set(SMOKE_PER_FILE_FLAGS "none")
endif()
target_include_directories(smoke_solver
  PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)
smoke_set_warnings(smoke_solver)

if(SMOKE_DISABLE_VECTORIZATION)
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(smoke_solver PRIVATE -fno-tree-vectorize)
    set(SMOKE_BASELINE_FLAGS "-fno-tree-vectorize")
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(smoke_solver PRIVATE -fno-vectorize -fno-slp-vectorize)
    set(SMOKE_BASELINE_FLAGS "-fno-vectorize -fno-slp-vectorize")
  elseif(MSVC)
    message(FATAL_ERROR "SMOKE_DISABLE_VECTORIZATION is currently supported with GCC and Clang only")
  else()
    message(FATAL_ERROR "SMOKE_DISABLE_VECTORIZATION is unsupported by ${CMAKE_CXX_COMPILER_ID}")
  endif()
  set(SMOKE_BENCH_VARIANT "B0")
else()
  set(SMOKE_BENCH_VARIANT "B1")
endif()

# The tests are compiled once into an OBJECT library and linked into both smoke_tests and
# smoke_cli (--selftest). OBJECT, not STATIC: tests register themselves through static objects
# that nothing references by name, so a static archive would let the linker drop them.
if(SMOKE_BUILD_CLI OR SMOKE_BUILD_TESTS)
  add_library(smoke_selftest OBJECT
    tests/selftest.cpp
    tests/test_field3d.cpp
    tests/test_solver_skeleton.cpp
    tests/test_boundary.cpp
    tests/test_interpolation.cpp
    tests/test_advection.cpp
    tests/test_sources.cpp
    tests/test_symmetry_determinism.cpp
    tests/test_forces.cpp
    tests/test_vorticity.cpp
    tests/test_projection.cpp
    tests/test_stability.cpp
    tests/demo_advect.cpp                              # M1 demo, started by "smoke_cli --demo advect"
  )
  target_link_libraries(smoke_selftest PUBLIC smoke_solver)
  target_include_directories(smoke_selftest
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/tests
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src            # tests check the internal stages directly
            ${CMAKE_CURRENT_SOURCE_DIR}/apps/common    # PgmWriter.h for the demo
  )
  # SYSTEM: warnings coming from inside doctest.h are not ours and must not hide our own warnings.
  target_include_directories(smoke_selftest SYSTEM PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
  smoke_set_warnings(smoke_selftest)
endif()

if(SMOKE_BUILD_CLI)
  add_executable(smoke_cli apps/smoke_cli/main.cpp)
  target_link_libraries(smoke_cli PRIVATE smoke_solver smoke_selftest)
  target_include_directories(smoke_cli PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/apps/common)
  target_compile_definitions(smoke_cli PRIVATE SMOKE_BUILD_TYPE="$<CONFIG>")
  smoke_set_warnings(smoke_cli)
endif()

# The commit id (for smoke_bench and smoke_lab output) is read when CMake CONFIGURES, not on every build.
# After a new commit, re-run cmake (or delete the build folder) before measuring, or a stale id is reported.
if(SMOKE_BUILD_BENCH OR SMOKE_BUILD_LAB)
  execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..
    OUTPUT_VARIABLE SMOKE_COMMIT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
  )
  if(NOT SMOKE_COMMIT)
    set(SMOKE_COMMIT "unknown")
  endif()
  execute_process(
    COMMAND git diff HEAD --quiet   # HEAD: staged changes also count as dirty
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..
    RESULT_VARIABLE SMOKE_WORKTREE_DIFF
    ERROR_QUIET
  )
  if(NOT SMOKE_WORKTREE_DIFF EQUAL 0)
    string(APPEND SMOKE_COMMIT "-dirty")
  endif()
endif()

if(SMOKE_BUILD_BENCH)
  add_executable(smoke_bench bench/bench_main.cpp)
  target_link_libraries(smoke_bench PRIVATE smoke_solver)
  target_compile_definitions(smoke_bench PRIVATE
    SMOKE_BENCH_BUILD_TYPE="$<CONFIG>"
    SMOKE_BENCH_COMPILER_ID="${CMAKE_CXX_COMPILER_ID}"
    SMOKE_BENCH_COMPILER_VERSION="${CMAKE_CXX_COMPILER_VERSION}"
    SMOKE_BENCH_VARIANT="${SMOKE_BENCH_VARIANT}"
    SMOKE_BENCH_FLAGS="${CMAKE_CXX_FLAGS_RELEASE} ${SMOKE_BASELINE_FLAGS}"
    SMOKE_BENCH_COMMIT="${SMOKE_COMMIT}"
    SMOKE_BENCH_AVX2_KERNELS="${SMOKE_AVX2_STATE}"
    SMOKE_BENCH_PER_FILE_FLAGS="${SMOKE_PER_FILE_FLAGS}"
  )
  smoke_set_warnings(smoke_bench)
endif()

# smoke_lab and smoke_demo: side-by-side versions for demonstrations (Solver/lab, Solver/demo; style of the
# lab example). The *_scalar.cpp / m3_b0.cpp files are compiled without vectorization, like the lab's
# *_scalar.cpp files; the others use the default Release flags.
if(SMOKE_BUILD_LAB)
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(SMOKE_NO_VECTORIZE_FLAGS "-fno-tree-vectorize")
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(SMOKE_NO_VECTORIZE_FLAGS "-fno-vectorize;-fno-slp-vectorize")
  else()
    set(SMOKE_NO_VECTORIZE_FLAGS "")
  endif()

  # Old (M3) kernel versions, AoS and unrolled demonstration versions, and the comparison tables.
  add_library(smoke_lab_kernels STATIC
    lab/LabRunner.cpp
    lab/jacobi_m3_scalar.cpp
    lab/jacobi_m3_auto.cpp
    lab/advection_m3_scalar.cpp
    lab/advection_m3_auto.cpp
    lab/advection_aos.cpp
  )
  target_link_libraries(smoke_lab_kernels PUBLIC smoke_solver)
  target_include_directories(smoke_lab_kernels PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/lab)
  set_source_files_properties(lab/jacobi_m3_scalar.cpp lab/advection_m3_scalar.cpp
                              PROPERTIES COMPILE_OPTIONS "${SMOKE_NO_VECTORIZE_FLAGS}")
  smoke_set_warnings(smoke_lab_kernels)

  add_executable(smoke_lab lab/lab_main.cpp)
  target_link_libraries(smoke_lab PRIVATE smoke_lab_kernels)
  target_compile_definitions(smoke_lab PRIVATE
    SMOKE_LAB_FLAGS="${CMAKE_CXX_FLAGS_RELEASE}"
    SMOKE_LAB_COMMIT="${SMOKE_COMMIT}")
  smoke_set_warnings(smoke_lab)

  # smoke_demo: the whole simulation, Neoptimizirana (M3, demo/M3Solver.inc) vs Optimizirana (M4) verzija.
  add_executable(smoke_demo demo/demo_main.cpp demo/m3_b0.cpp demo/m3_b1.cpp)
  target_link_libraries(smoke_demo PRIVATE smoke_lab_kernels)
  target_include_directories(smoke_demo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/demo)
  set_source_files_properties(demo/m3_b0.cpp PROPERTIES COMPILE_OPTIONS "${SMOKE_NO_VECTORIZE_FLAGS}")
  target_compile_definitions(smoke_demo PRIVATE
    SMOKE_DEMO_FLAGS="${CMAKE_CXX_FLAGS_RELEASE}"
    SMOKE_DEMO_COMMIT="${SMOKE_COMMIT}")
  smoke_set_warnings(smoke_demo)

  # MinGW: link the C++ runtime statically into the demonstration programs, so the .exe also starts on a
  # computer without MSYS2 (no libstdc++-6.dll, libgcc_s_seh-1.dll, libwinpthread-1.dll needed).
  if(MINGW)
    target_link_options(smoke_demo PRIVATE -static)
    target_link_options(smoke_lab PRIVATE -static)
  endif()
endif()

if(SMOKE_BUILD_TESTS)
  enable_testing()
  add_executable(smoke_tests tests/test_main.cpp)
  target_link_libraries(smoke_tests PRIVATE smoke_solver smoke_selftest)
  smoke_set_warnings(smoke_tests)
  add_test(NAME smoke_tests COMMAND smoke_tests)
endif()
