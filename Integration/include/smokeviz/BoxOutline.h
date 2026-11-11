#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <string>

class Camera;
class ShaderProgram;

namespace smokeviz {

/// The 12 edges of the simulation box, drawn as lines, so the "invisible box" (docs/plan/ARCHITECTURE.md,
/// section 8) can be seen while developing.
class BoxOutline {
public:
    BoxOutline();
    ~BoxOutline();

    BoxOutline(const BoxOutline&)            = delete;
    BoxOutline& operator=(const BoxOutline&) = delete;

    bool Init(const std::string& shaderDir);
    void Draw(const Camera& camera, const glm::vec3& boxMin, const glm::vec3& boxSize, const glm::vec3& color) const;

private:
    std::unique_ptr<ShaderProgram> m_shader;
    uint32_t                       m_vao = 0;
    uint32_t                       m_vbo = 0;
};

} // namespace smokeviz
