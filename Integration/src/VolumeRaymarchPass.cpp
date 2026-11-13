#include "smokeviz/VolumeRaymarchPass.h"

#include "smokeviz/Texture3D.h"

#include "core/Camera.h"
#include "core/ShaderProgram.h"

#include <glad/glad.h>
#include <glm/gtc/matrix_inverse.hpp>

#include <algorithm>

namespace smokeviz {

VolumeRaymarchPass::VolumeRaymarchPass() = default;

VolumeRaymarchPass::~VolumeRaymarchPass() {
    if (m_vao != 0) glDeleteVertexArrays(1, &m_vao);
}

bool VolumeRaymarchPass::Init(const std::string& shaderDir) {
    m_shader = std::make_unique<ShaderProgram>(shaderDir + "/volume_raymarch.vert", shaderDir + "/volume_raymarch.frag");
    if (m_vao == 0) glGenVertexArrays(1, &m_vao);
    return m_shader->IsValid();
}

void VolumeRaymarchPass::Draw(const Camera& camera, const Texture3D& density, const glm::vec3& boxMin,
                              const glm::vec3& boxSize, const VolumeSettings& settings, const glm::vec3& toLight,
                              const glm::vec3& lightColor) const {
    if (!m_shader || !m_shader->IsValid() || !density.IsValid()) return;

    // The smoke is transparent: it must not write depth, and it is blended over the opaque scene.
    // The fixed depth test is off; instead the shader reads the scene depth and stops each ray there, so
    // objects in front of or inside the smoke hide the right part of it (decision D13).
    const GLboolean hadCull = glIsEnabled(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    // The shader outputs colour already multiplied by its opacity ("premultiplied alpha"):
    // result = smoke + background * (1 - opacity).
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    m_shader->Bind();
    m_shader->SetUniform("u_Density", 0);
    m_shader->SetUniform("u_InvViewProj", glm::inverse(camera.GetViewProjection()));
    m_shader->SetUniform("u_CameraPos", camera.GetPosition());
    m_shader->SetUniform("u_BoxMin", boxMin);
    m_shader->SetUniform("u_BoxSize", boxSize);
    m_shader->SetUniform("u_Steps", std::clamp(settings.steps, 8, 512));
    m_shader->SetUniform("u_LightSteps", std::clamp(settings.lightSteps, 0, 32));
    m_shader->SetUniform("u_Absorption", settings.absorption);
    m_shader->SetUniform("u_ShadowStrength", settings.shadowStrength);
    m_shader->SetUniform("u_Ambient", settings.ambient);
    m_shader->SetUniform("u_SmokeColor", settings.smokeColor);
    m_shader->SetUniform("u_ToLight", glm::normalize(toLight));
    m_shader->SetUniform("u_LightColor", lightColor * settings.lightIntensity);
    m_shader->SetUniform("u_Jitter", settings.jitter ? 1.0f : 0.0f);

    // Scene depth (decision D13): the HDR framebuffer bound right now has its depth in a texture. Ask OpenGL
    // for it instead of reaching into the Renderer, so the Renderer's interface does not have to change.
    // Sampling it while the framebuffer is bound is fine here because this pass neither tests nor writes depth.
    GLint depthType = GL_NONE, depthTexture = 0;
    glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                          GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &depthType);
    if (depthType == GL_TEXTURE)
        glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                              GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &depthTexture);
    const bool useDepth = settings.occlusion && depthTexture != 0;
    m_shader->SetUniform("u_SceneDepth", 1);
    m_shader->SetUniform("u_UseSceneDepth", useDepth);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, useDepth ? static_cast<GLuint>(depthTexture) : 0);

    density.Bind(0);
    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_3D, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    ShaderProgram::Unbind();

    // Leave the state the way the Renderer expects it for the next pass.
    glDisable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    if (hadCull) glEnable(GL_CULL_FACE);
}

} // namespace smokeviz
