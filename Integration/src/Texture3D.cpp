#include "smokeviz/Texture3D.h"

#include "smoke/DensityView.h"

#include <glad/glad.h>

#include <cstddef>
#include <cstring>

namespace smokeviz {

Texture3D::~Texture3D() {
    if (m_id != 0) glDeleteTextures(1, &m_id);
}

void Texture3D::Allocate(int nx, int ny, int nz) {
    if (m_id != 0) glDeleteTextures(1, &m_id);
    glGenTextures(1, &m_id);
    glBindTexture(GL_TEXTURE_3D, m_id);
    // GL_R32F: one 32-bit float per texel, the same type the solver uses, so the driver copies without converting.
    glTexImage3D(GL_TEXTURE_3D, 0, GL_R32F, nx, ny, nz, 0, GL_RED, GL_FLOAT, nullptr);
    // Linear filtering gives trilinear interpolation between cell centres, the same idea the solver uses in
    // advection. Clamp: outside the box the shader never samples (the ray is cut at the box).
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    m_nx = nx;
    m_ny = ny;
    m_nz = nz;
}

void Texture3D::Upload(const smoke::DensityView& view) {
    if (view.origin == nullptr || view.nx <= 0 || view.ny <= 0 || view.nz <= 0) return;
    if (m_id == 0 || view.nx != m_nx || view.ny != m_ny || view.nz != m_nz) Allocate(view.nx, view.ny, view.nz);

    glBindTexture(GL_TEXTURE_3D, m_id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

    // The solver stores rows with padding and a ghost layer: the next row is strideJ floats further, the next
    // plane strideK floats further. OpenGL can read such memory directly if told the real row length and the
    // real number of rows per plane. That needs a whole number of rows per plane.
    const bool directLayout = view.strideJ >= view.nx && view.strideK % view.strideJ == 0 &&
                              view.strideK / view.strideJ >= view.ny;
    if (directLayout) {
        glPixelStorei(GL_UNPACK_ROW_LENGTH, view.strideJ);
        glPixelStorei(GL_UNPACK_IMAGE_HEIGHT, view.strideK / view.strideJ);
        glTexSubImage3D(GL_TEXTURE_3D, 0, 0, 0, 0, view.nx, view.ny, view.nz, GL_RED, GL_FLOAT, view.origin);
        // Back to the defaults: these settings are global and the Renderer's own texture uploads rely on them.
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glPixelStorei(GL_UNPACK_IMAGE_HEIGHT, 0);
    } else {
        // Fallback: copy the interior into a tightly packed buffer first.
        m_packed.resize(static_cast<std::size_t>(view.nx) * view.ny * view.nz);
        float* out = m_packed.data();
        for (int k = 0; k < view.nz; ++k)
            for (int j = 0; j < view.ny; ++j) {
                const float* row = view.origin + static_cast<std::ptrdiff_t>(view.strideJ) * j +
                                   static_cast<std::ptrdiff_t>(view.strideK) * k;
                std::memcpy(out, row, sizeof(float) * static_cast<std::size_t>(view.nx));
                out += view.nx;
            }
        glTexSubImage3D(GL_TEXTURE_3D, 0, 0, 0, 0, view.nx, view.ny, view.nz, GL_RED, GL_FLOAT, m_packed.data());
    }
    glBindTexture(GL_TEXTURE_3D, 0);
}

void Texture3D::Bind(uint32_t unit) const {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_3D, m_id);
}

} // namespace smokeviz
