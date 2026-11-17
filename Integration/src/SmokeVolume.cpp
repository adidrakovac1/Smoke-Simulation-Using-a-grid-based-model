#include "smokeviz/SmokeVolume.h"

#include "core/Camera.h"
#include "core/Mesh.h"
#include "core/ShaderProgram.h"
#include "smokeviz/Screenshot.h"
#include "smoke/SmokeSolver.h"

#include <glad/glad.h>
#include <spdlog/spdlog.h>
#include <chrono>
#include <cmath>

namespace smokeviz {

SmokeVolume::SmokeVolume() = default;
SmokeVolume::~SmokeVolume() = default;

bool SmokeVolume::Initialize(const glm::vec3& boxMin, const glm::vec3& boxSize, smoke::GridSize grid) {
    m_boxMin = boxMin;
    m_boxSize = boxSize;
    m_solver = std::make_unique<smoke::SmokeSolver>();
    if (grid.nx != 64 || grid.ny != 64 || grid.nz != 64)
        m_solver->resize(grid);

    if (!m_volumePass.Init(SMOKE_SHADER_DIR) || !m_outline.Init(SMOKE_SHADER_DIR)) {
        spdlog::error("[SmokeVolume] failed to initialize volume shaders");
        m_solver.reset();
        return false;
    }
    m_obstacleShader = std::make_unique<ShaderProgram>(
        std::string(SMOKE_SHADER_DIR) + "/obstacle_mesh.vert",
        std::string(SMOKE_SHADER_DIR) + "/obstacle_mesh.frag");
    if (!m_obstacleShader->IsValid())
        spdlog::warn("[SmokeVolume] obstacle shader unavailable; obstacle drawing is disabled");
    m_densityDirty = true;
    return true;
}

void SmokeVolume::Update() {
    if (!m_solver || !m_state.enabled || m_state.paused)
        return;
    m_solver->step();
    m_densityDirty = true;
}

void SmokeVolume::Render(const Camera& camera, const glm::vec3& toLight, const glm::vec3& lightColor) {
    if (!m_solver || !m_state.enabled)
        return;

    if (m_densityDirty) {
        const auto start = std::chrono::steady_clock::now();
        m_densityTexture.Upload(m_solver->density());
        m_state.uploadMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
        m_densityDirty = false;
    }
    if (m_state.obstacleActive && !m_obstaclePorous)
        DrawObstacle(camera, toLight, false);
    if (m_state.showOutline)
        m_outline.Draw(camera, m_boxMin, m_boxSize, {0.55f, 0.62f, 0.70f});
    m_volumePass.Draw(camera, m_densityTexture, m_boxMin, m_boxSize, m_view, toLight, lightColor);
    if (m_state.obstacleActive && m_obstaclePorous)
        DrawObstacle(camera, toLight, true);
}

void SmokeVolume::DrawUI() {
    if (!m_solver)
        return;

    const SmokeUiActions actions = m_ui.Draw(*m_solver, m_view, m_state, m_obstacleSettings,
                                             true, "Dim na terenu", &m_boxMin, &m_boxSize);
    if (actions.applyGrid) {
        m_solver->resize({actions.newGrid, actions.newGrid, actions.newGrid});
        m_densityDirty = true;
        if (m_state.obstacleActive)
            ApplyObstacle();
    }
    if (actions.reset) {
        m_solver->reset();
        m_densityDirty = true;
    }
    if (actions.stepOnce) {
        m_solver->step();
        m_densityDirty = true;
    }
    if (actions.applyObstacle)
        ApplyObstacle();
    if (actions.removeObstacle)
        RemoveObstacle();
    if (actions.boxTransformChanged && m_state.obstacleActive)
        m_obstacle.Place(m_obstacleSettings, m_boxMin, m_boxSize);
    if (actions.boxTransformCommitted && m_state.obstacleActive)
        ApplyObstacle();
    if (actions.screenshot)
        SaveWindowPng("smoke_screenshot_terrain.png");
}

void SmokeVolume::ApplyObstacle() {
    std::string error;
    if (m_loadedObstaclePath != m_obstacleSettings.path || !m_obstacle.RenderMesh()) {
      if (!m_obstacle.Load(m_obstacleSettings.path, &error)) {
        m_state.obstacleMessage = "Greska: " + error;
        spdlog::warn("[SmokeVolume] obstacle: {}", error);
        return;
      }
      m_loadedObstaclePath = m_obstacleSettings.path;
    }

    m_obstacle.Place(m_obstacleSettings, m_boxMin, m_boxSize);
    smoke::ObstacleGrid grid(m_solver->gridSize());
    smoke::VoxelizeOptions options;
    options.mode = m_obstacleSettings.mode == 2 ? smoke::ObstacleMode::Porous
                   : m_obstacleSettings.mode == 1 ? smoke::ObstacleMode::Shell
                                                  : smoke::ObstacleMode::Solid;
    if (options.mode == smoke::ObstacleMode::Shell)
        options.shellHalfThickness = 1.0f;
    const smoke::VoxelizeStats stats = smoke::voxelize(m_obstacle.BoxMesh(), options, grid);
    grid.porousDrag = m_obstacleSettings.porousDrag;
    grid.porousDensityFilter = m_obstacleSettings.porousFilter;
    if (!m_solver->setObstacles(grid)) {
        m_state.obstacleMessage = "Greska: prepreka ne odgovara velicini mreze.";
        return;
    }

    m_obstaclePorous = options.mode == smoke::ObstacleMode::Porous;
    m_state.obstacleActive = true;
    m_state.obstacleMessage = std::to_string(m_obstacle.TriangleCount()) + " trouglova; celija: cvrstih " +
        std::to_string(grid.count(smoke::CellType::Solid)) + ", poroznih " +
        std::to_string(grid.count(smoke::CellType::Porous)) + " (povrsina " +
        std::to_string(stats.surfaceCells) + ", unutra " + std::to_string(stats.interiorCells) + ")";
    m_densityDirty = true;
}

void SmokeVolume::RemoveObstacle() {
    m_solver->clearObstacles();
    m_state.obstacleActive = false;
    m_obstaclePorous = false;
    m_state.obstacleMessage = "Bez prepreke.";
    m_densityDirty = true;
}

void SmokeVolume::DrawObstacle(const Camera& camera, const glm::vec3& toLight, bool porous) {
    const Mesh* mesh = m_obstacle.RenderMesh();
    if (!mesh || !m_obstacleShader || !m_obstacleShader->IsValid())
        return;

    const GLboolean hadDepth = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean hadBlend = glIsEnabled(GL_BLEND);
    const GLboolean hadCull = glIsEnabled(GL_CULL_FACE);
    GLboolean oldDepthMask = GL_TRUE;
    GLint oldCullMode = GL_BACK;
    GLint oldBlendSrcRgb = GL_ONE, oldBlendDstRgb = GL_ZERO;
    GLint oldBlendSrcAlpha = GL_ONE, oldBlendDstAlpha = GL_ZERO;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &oldDepthMask);
    glGetIntegerv(GL_CULL_FACE_MODE, &oldCullMode);
    glGetIntegerv(GL_BLEND_SRC_RGB, &oldBlendSrcRgb);
    glGetIntegerv(GL_BLEND_DST_RGB, &oldBlendDstRgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &oldBlendSrcAlpha);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &oldBlendDstAlpha);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glDepthMask(porous ? GL_FALSE : GL_TRUE);
    if (porous) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    } else {
        glDisable(GL_BLEND);
    }

    m_obstacleShader->Bind();
    m_obstacleShader->SetUniform("u_ViewProj", camera.GetViewProjection());
    m_obstacleShader->SetUniform("u_ToLight", glm::normalize(toLight));
    m_obstacleShader->SetUniform("u_Color", porous ? glm::vec3(0.75f, 0.55f, 0.25f)
                                                    : glm::vec3(0.32f, 0.22f, 0.16f));
    m_obstacleShader->SetUniform("u_Alpha", porous ? 0.45f : 1.0f);
    mesh->DrawAll();
    ShaderProgram::Unbind();

    glBlendFuncSeparate(static_cast<GLenum>(oldBlendSrcRgb), static_cast<GLenum>(oldBlendDstRgb),
                        static_cast<GLenum>(oldBlendSrcAlpha), static_cast<GLenum>(oldBlendDstAlpha));
    glDepthMask(oldDepthMask);
    glCullFace(static_cast<GLenum>(oldCullMode));
    if (hadDepth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (hadBlend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    if (hadCull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
}

} // namespace smokeviz
