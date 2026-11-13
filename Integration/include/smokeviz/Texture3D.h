#pragma once

#include <cstdint>
#include <vector>

namespace smoke {
struct DensityView;
}

namespace smokeviz {

/// One-channel float 3D texture that holds the smoke density on the GPU.
///
/// The Renderer has 2D textures and cubemaps only, so this class lives in Integration
/// (docs/plan/ARCHITECTURE.md, section 6). Texel (i, j, k) is interior cell (i, j, k) of the solver grid;
/// with linear filtering the shader can read the density at any point inside the box.
class Texture3D {
public:
    Texture3D() = default;
    ~Texture3D();

    Texture3D(const Texture3D&)            = delete;
    Texture3D& operator=(const Texture3D&) = delete;

    /// Copies the density field to the GPU. Allocates the texture on first use and again whenever the grid
    /// size changes; otherwise only the contents are replaced (glTexSubImage3D).
    void Upload(const smoke::DensityView& view);

    /// Binds the texture to the given texture unit (0, 1, ...).
    void Bind(uint32_t unit) const;

    [[nodiscard]] bool IsValid() const { return m_id != 0; }
    [[nodiscard]] int  Width() const { return m_nx; }
    [[nodiscard]] int  Height() const { return m_ny; }
    [[nodiscard]] int  Depth() const { return m_nz; }

private:
    void Allocate(int nx, int ny, int nz);

    uint32_t           m_id = 0;
    int                m_nx = 0, m_ny = 0, m_nz = 0;
    std::vector<float> m_packed;  // used only if the solver layout cannot be described to OpenGL directly
};

} // namespace smokeviz
