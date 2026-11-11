#include "smokeviz/BoxOutline.h"

#include "core/Camera.h"
#include "core/ShaderProgram.h"

#include <glad/glad.h>

namespace smokeviz {

BoxOutline::BoxOutline() = default;

BoxOutline::~BoxOutline() {
    if (m_vbo != 0) glDeleteBuffers(1, &m_vbo);
    if (m_vao != 0) glDeleteVertexArrays(1, &m_vao);
}

bool BoxOutline::Init(const std::string& shaderDir) {
    m_shader = std::make_unique<ShaderProgram>(shaderDir + "/box_outline.vert", shaderDir + "/box_outline.frag");

    // 12 edges of the unit cube, two corners each.
    static const float kEdges[] = {
        0, 0, 0, 1, 0, 0,   1, 0, 0, 1, 0, 1,   1, 0, 1, 0, 0, 1,   0, 0, 1, 0, 0, 0,   // bottom
        0, 1, 0, 1, 1, 0,   1, 1, 0, 1, 1, 1,   1, 1, 1, 0, 1, 1,   0, 1, 1, 0, 1, 0,   // top
        0, 0, 0, 0, 1, 0,   1, 0, 0, 1, 1, 0,   1, 0, 1, 1, 1, 1,   0, 0, 1, 0, 1, 1,   // verticals
    };
    if (m_vao == 0) glGenVertexArrays(1, &m_vao);
    if (m_vbo == 0) glGenBuffers(1, &m_vbo);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kEdges), kEdges, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    return m_shader->IsValid();
}

void BoxOutline::Draw(const Camera& camera, const glm::vec3& boxMin, const glm::vec3& boxSize,
                      const glm::vec3& color) const {
    if (!m_shader || !m_shader->IsValid() || m_vao == 0) return;
    // Depth test on, so the floor and other opaque objects hide the edges behind them.
    glEnable(GL_DEPTH_TEST);
    m_shader->Bind();
    m_shader->SetUniform("u_ViewProj", camera.GetViewProjection());
    m_shader->SetUniform("u_BoxMin", boxMin);
    m_shader->SetUniform("u_BoxSize", boxSize);
    m_shader->SetUniform("u_Color", color);
    glBindVertexArray(m_vao);
    glDrawArrays(GL_LINES, 0, 24);
    glBindVertexArray(0);
    ShaderProgram::Unbind();
}

} // namespace smokeviz
