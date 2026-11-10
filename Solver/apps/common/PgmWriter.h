#pragma once

// Writes one slice of the density field as a PGM image (docs/plan/VERIFICATION.md, section 5).
//
// PGM ("portable graymap") is the simplest image format there is: a short text header
// "P5 <width> <height> 255" followed by one byte (0 = black, 255 = white) per pixel. No library is
// needed to write it, and GIMP, IrfanView or VS Code (with an image extension) can open it.
// Header-only, shared by smoke_cli and the M1 advection demo. Not part of the solver library.

#include "smoke/DensityView.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <string>
#include <vector>

namespace smoke_io {

/// Largest density in the x-y plane with interior index k (0-based, 0 <= k < v.nz).
inline float maxInSliceXY(const smoke::DensityView& v, int k) {
    float m = 0.0f;
    for (int j = 0; j < v.ny; ++j)
        for (int i = 0; i < v.nx; ++i) m = std::max(m, v.origin[i + v.strideJ * j + v.strideK * k]);
    return m;
}

/// Writes the x-y plane with interior index k (0-based) as a binary PGM, nx pixels wide and ny high.
/// "Up" (+y, the buoyancy direction) is at the top of the image, so the first image row is j = ny - 1.
/// Pixel = clamp(density / maxValue, 0, 1) * 255; if maxValue <= 0 the image is black.
/// Returns false if the file cannot be written.
inline bool writePgmSliceXY(const std::string& path, const smoke::DensityView& v, int k, float maxValue) {
    if (v.origin == nullptr || k < 0 || k >= v.nz) return false;
    std::vector<unsigned char> pixels(static_cast<std::size_t>(v.nx) * static_cast<std::size_t>(v.ny));
    const float scale = maxValue > 0.0f ? 255.0f / maxValue : 0.0f;
    for (int row = 0; row < v.ny; ++row) {
        const int j = v.ny - 1 - row;
        for (int i = 0; i < v.nx; ++i) {
            const float value = v.origin[i + v.strideJ * j + v.strideK * k] * scale;
            pixels[static_cast<std::size_t>(row) * v.nx + i] =
                static_cast<unsigned char>(std::lround(std::clamp(value, 0.0f, 255.0f)));
        }
    }
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out << "P5\n" << v.nx << " " << v.ny << "\n255\n";
    out.write(reinterpret_cast<const char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
    return static_cast<bool>(out);
}

} // namespace smoke_io
