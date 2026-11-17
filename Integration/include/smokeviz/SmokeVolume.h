#pragma once

#include "smoke/GridSize.h"
#include "smokeviz/BoxOutline.h"
#include "smokeviz/ObstacleObject.h"
#include "smokeviz/SmokeUI.h"
#include "smokeviz/Texture3D.h"
#include "smokeviz/VolumeRaymarchPass.h"

#include <glm/glm.hpp>

#include <memory>
#include <string>

class Camera;
class ShaderProgram;

namespace smoke {
class SmokeSolver;
}

namespace smokeviz {

/// Reusable smoke simulation and volume renderer that can be owned by any Scene.
/// The host scene supplies its own camera and light and calls Update/Render from its hooks.
class SmokeVolume {
public:
    SmokeVolume();
    ~SmokeVolume();

    SmokeVolume(const SmokeVolume&) = delete;
    SmokeVolume& operator=(const SmokeVolume&) = delete;

    bool Initialize(const glm::vec3& boxMin, const glm::vec3& boxSize,
                    smoke::GridSize grid = {64, 64, 64});

    void Update();
    void Render(const Camera& camera, const glm::vec3& toLight, const glm::vec3& lightColor);
    void DrawUI();

    void SetEnabled(bool enabled) { m_state.enabled = enabled; }
    [[nodiscard]] bool IsEnabled() const { return m_state.enabled; }

private:
    void ApplyObstacle();
    void RemoveObstacle();
    void DrawObstacle(const Camera& camera, const glm::vec3& toLight, bool porous);

    std::unique_ptr<smoke::SmokeSolver> m_solver;
    Texture3D m_densityTexture;
    VolumeRaymarchPass m_volumePass;
    BoxOutline m_outline;
    VolumeSettings m_view;
    SmokeUI m_ui;
    SmokeUiState m_state;
    ObstacleSettings m_obstacleSettings;
    ObstacleObject m_obstacle;
    std::unique_ptr<ShaderProgram> m_obstacleShader;
    bool m_obstaclePorous = false;
    std::string m_loadedObstaclePath;
    glm::vec3 m_boxMin{0.0f};
    glm::vec3 m_boxSize{1.0f};
    bool m_densityDirty = true;
};

} // namespace smokeviz
