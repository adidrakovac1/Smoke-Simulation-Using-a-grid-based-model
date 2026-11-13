cmake_minimum_required(VERSION 3.26.0)
project(TestApp)

set(CMAKE_CXX_STANDARD 20)

# Test application executable
add_executable(${PROJECT_NAME}
        src/main.cpp
        src/CapturePresetScene.h
        src/CapturePresetScene.cpp
        src/SampleScene.cpp
        src/SolarSystemScene.h
        src/SolarSystemScene.cpp
        src/DioramaScene.h
        src/DioramaScene.cpp
        src/NeonCityScene.h
        src/NeonCityScene.cpp
        src/JapanScene.h
        src/JapanScene.cpp
        src/PbrValidationScene.h
        src/PbrValidationScene.cpp
        src/NormalMapScene.h
        src/NormalMapScene.cpp
        src/IblValidationScene.h
        src/IblValidationScene.cpp
        src/BistroScene.h
        src/BistroScene.cpp
        src/TerrainScene.h
        src/TerrainScene.cpp
)

# Smoke simulation (docs/plan/ARCHITECTURE.md, section 6, allowed change 1): the solver library and the
# Integration layer that draws it. Only the solver library is built here; its console programs, tests and
# benchmarks are built from Solver/ on its own.
set(SMOKE_BUILD_CLI   OFF CACHE BOOL "" FORCE)
set(SMOKE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SMOKE_BUILD_BENCH OFF CACHE BOOL "" FORCE)
set(SMOKE_BUILD_LAB   OFF CACHE BOOL "" FORCE)
set(SMOKE_BUILD_PROF  OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../Solver ${CMAKE_BINARY_DIR}/smoke-solver)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../Integration ${CMAKE_BINARY_DIR}/smoke-integration)

target_link_libraries(${PROJECT_NAME} PRIVATE
        RendererLib
        SmokeIntegration
)

# Copy shaders to test app output directory
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/assets"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/assets"
        COMMENT "Copying assets to test app output directory"
)
