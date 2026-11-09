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
)
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

if(SMOKE_BUILD_BENCH)
  add_executable(smoke_bench bench/bench_main.cpp)
  target_link_libraries(smoke_bench PRIVATE smoke_solver)
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
    COMMAND git diff --quiet
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..
    RESULT_VARIABLE SMOKE_WORKTREE_DIFF
    ERROR_QUIET
  )
  if(NOT SMOKE_WORKTREE_DIFF EQUAL 0)
    string(APPEND SMOKE_COMMIT "-dirty")
  endif()
  target_compile_definitions(smoke_bench PRIVATE
    SMOKE_BENCH_BUILD_TYPE="$<CONFIG>"
    SMOKE_BENCH_COMPILER_ID="${CMAKE_CXX_COMPILER_ID}"
    SMOKE_BENCH_COMPILER_VERSION="${CMAKE_CXX_COMPILER_VERSION}"
    SMOKE_BENCH_VARIANT="${SMOKE_BENCH_VARIANT}"
    SMOKE_BENCH_FLAGS="${CMAKE_CXX_FLAGS_RELEASE} ${SMOKE_BASELINE_FLAGS}"
    SMOKE_BENCH_COMMIT="${SMOKE_COMMIT}"
  )
  smoke_set_warnings(smoke_bench)
endif()

if(SMOKE_BUILD_TESTS)
  enable_testing()
  add_executable(smoke_tests tests/test_main.cpp)
  target_link_libraries(smoke_tests PRIVATE smoke_solver smoke_selftest)
  smoke_set_warnings(smoke_tests)
  add_test(NAME smoke_tests COMMAND smoke_tests)
endif()
