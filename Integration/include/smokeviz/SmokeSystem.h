#pragma once

#include "smoke/GridSize.h"
#include "smokeviz/SmokeVolume.h"

#include <glm/glm.hpp>
#include <memory>
#include <vector>

class Camera;

namespace smokeviz {

/// A scene's collection of independent smoke boxes (SmokeVolume) and the "Dimovi" window that adds, removes and
/// edits them. Used by every scene behind the renderer's global Smoke menu: the dedicated smoke scene, Terrain,
/// and (through Scene::SetSmokeCallbacks in main.cpp) every other scene. Each box runs its own solver with the
/// shared D19/D20 settings (smokeviz/SolverSetup.h).
class SmokeSystem {
public:
    bool Initialize();

    /// Position, size and grid the "Dodaj dim" button starts from.
    void SetNewVolumeDefaults(const glm::vec3& position, const glm::vec3& size, const smoke::GridSize& grid);
    /// Number shown for the first box ("Dim N"); the smoke scene starts at 2 because its own box is "Dim 1".
    void SetFirstLabel(int label) { m_firstLabel = label; }
    /// Light used by Render(camera); scenes with their own light pass it here.
    void SetLight(const glm::vec3& toLight, const glm::vec3& color) { m_toLight = toLight; m_lightColor = color; }

    bool AddVolume(const glm::vec3& position, const glm::vec3& size);
    bool AddVolume(const glm::vec3& position, const glm::vec3& size, const smoke::GridSize& grid);

    void Update();
    void Render(const Camera& camera);
    void DrawUI(bool visible);

    [[nodiscard]] std::size_t VolumeCount() const { return m_volumes.size(); }

private:
    std::vector<std::unique_ptr<SmokeVolume>> m_volumes;
    glm::vec3       m_newPosition{-6.0f, 0.0f, -6.0f};
    glm::vec3       m_newSize{12.0f, 18.0f, 12.0f};
    smoke::GridSize m_newGrid{32, 32, 32};
    int             m_firstLabel = 1;
    glm::vec3       m_toLight{0.5f, 1.0f, 0.4f};
    glm::vec3       m_lightColor{1.0f, 0.97f, 0.88f};
};

} // namespace smokeviz
