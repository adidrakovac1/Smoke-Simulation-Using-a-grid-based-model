#pragma once

#include "smokeviz/SmokeVolume.h"

#include <glm/glm.hpp>
#include <memory>
#include <vector>

class Camera;

namespace smokeviz {

/// Per-scene collection used by the renderer's global Smoke menu.
class SmokeSystem {
public:
    bool Initialize();
    void Update();
    void Render(const Camera& camera);
    void DrawUI(bool visible);

private:
    void AddVolume(const glm::vec3& position, const glm::vec3& size);

    std::vector<std::unique_ptr<SmokeVolume>> m_volumes;
    glm::vec3 m_newPosition{-6.0f, 0.0f, -6.0f};
    glm::vec3 m_newSize{12.0f, 18.0f, 12.0f};
};

} // namespace smokeviz
