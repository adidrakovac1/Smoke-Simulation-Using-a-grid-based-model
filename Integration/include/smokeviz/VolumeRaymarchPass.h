#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <string>

class Camera;
class ShaderProgram;

namespace smokeviz {

class Texture3D;

/// How the smoke is drawn. None of this changes the simulation; these are display settings.
struct VolumeSettings {
    int       steps          = 96;     ///< samples along each view ray inside the box
    int       lightSteps     = 6;      ///< samples from each sample towards the light (0 = no self-shadowing)
    float     absorption     = 6.0f;   ///< how strongly density blocks light, per unit of density and world length
    float     shadowStrength = 1.0f;   ///< scales the absorption on the way to the light
    float     ambient        = 0.12f;  ///< light the smoke receives regardless of shadowing
    float     lightIntensity = 0.75f;  ///< kept below 1 so lit smoke is not clipped to white by tone mapping
    glm::vec3 smokeColor{0.85f, 0.85f, 0.88f};
    bool      jitter = true;           ///< shift the first sample per pixel to hide banding
};

/// Draws the density texture as a semi-transparent volume by ray marching (Crane et al., GPU Gems 3, ch. 30).
///
/// For every pixel a ray goes from the camera into the scene. The part of the ray inside the box is sampled in
/// a fixed number of steps; each sample absorbs some light (Beer-Lambert law: transmittance = exp(-sigma * rho * ds))
/// and adds its own lit colour. The result is blended over what is already in the HDR framebuffer.
class VolumeRaymarchPass {
public:
    VolumeRaymarchPass();
    ~VolumeRaymarchPass();

    VolumeRaymarchPass(const VolumeRaymarchPass&)            = delete;
    VolumeRaymarchPass& operator=(const VolumeRaymarchPass&) = delete;

    /// Loads the shaders from the given folder. Needs a current OpenGL context.
    bool Init(const std::string& shaderDir);

    /// @param boxMin, boxSize  the axis-aligned box in world space that the grid fills
    /// @param toLight          unit vector from the smoke towards the light
    void Draw(const Camera& camera, const Texture3D& density, const glm::vec3& boxMin, const glm::vec3& boxSize,
              const VolumeSettings& settings, const glm::vec3& toLight, const glm::vec3& lightColor) const;

private:
    std::unique_ptr<ShaderProgram> m_shader;
    uint32_t                       m_vao = 0;  // empty: the vertex shader makes a full-screen triangle from gl_VertexID
};

} // namespace smokeviz
