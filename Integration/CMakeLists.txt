cmake_minimum_required(VERSION 3.26)
project(SmokeIntegration LANGUAGES CXX)

# The only place that knows both the Solver and the Renderer (docs/plan/ARCHITECTURE.md, sections 1 and 6).
# It is not a standalone project: Renderer/test-app/CMakeLists.txt adds the Solver and this folder, so the
# targets RendererLib and smoke_solver already exist here.
if(NOT TARGET RendererLib OR NOT TARGET smoke_solver)
  message(FATAL_ERROR "SmokeIntegration is built from Renderer/test-app (needs RendererLib and smoke_solver)")
endif()

add_library(SmokeIntegration STATIC
  src/SmokeScene.cpp
  src/Texture3D.cpp
  src/VolumeRaymarchPass.cpp
  src/BoxOutline.cpp
  src/SmokeUI.cpp
  src/Screenshot.cpp
)

target_compile_features(SmokeIntegration PUBLIC cxx_std_20)
target_include_directories(SmokeIntegration PUBLIC include)
target_link_libraries(SmokeIntegration PUBLIC RendererLib smoke_solver)

# The shaders are read from the source folder at run time, so they work from any working directory and a shader
# edit needs no rebuild. (The Renderer loads its own shaders relative to the working directory; we do not put
# files into Renderer/assets because Renderer/ is not our code.)
target_compile_definitions(SmokeIntegration PRIVATE
  SMOKE_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shaders"
)
