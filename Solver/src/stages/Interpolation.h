#pragma once

// Small helpers used per cell inside the advection loop. They are inline in a header so the compiler
// can inline them into the innermost loop (docs/plan/ARCHITECTURE.md, section 5, rule 4).

#include "smoke/Field3D.h"

namespace smoke::serial {

/// Clamp a back-traced coordinate to the box. Interior cell i covers [i - 0.5, i + 0.5], so the box
/// spans [0.5, n + 0.5] along an axis with n interior cells. This is the clamp used by advect() in
/// Stam 2003, and it matches clipping the path at the wall in Fedkiw et al. 2001, fig. 2
/// (docs/plan/MATH.md, section 4).
inline float clampToBox(float x, int n) {
    const float lo = 0.5f;
    const float hi = static_cast<float>(n) + 0.5f;
    return x < lo ? lo : (x > hi ? hi : x);
}

/// Trilinear interpolation of `f` at the point (x, y, z), given in cell-index coordinates
/// (cell (i,j,k) has its centre at (i,j,k)). The point must already be clamped with clampToBox, so all
/// eight neighbours exist (they may be ghost cells, which setBoundary keeps up to date).
///
/// The result is a weighted average of the 8 surrounding values with weights >= 0 that sum to 1, so it
/// always lies between their minimum and maximum (test T4, docs/plan/VERIFICATION.md).
inline float sampleTrilinear(const Field3D& f, float x, float y, float z) {
    // x >= 0.5 after clamping, so the int conversion (which truncates) is the same as floor().
    const int i0 = static_cast<int>(x);
    const int j0 = static_cast<int>(y);
    const int k0 = static_cast<int>(z);
    const int i1 = i0 + 1;
    const int j1 = j0 + 1;
    const int k1 = k0 + 1;

    const float sx1 = x - static_cast<float>(i0), sx0 = 1.0f - sx1;
    const float sy1 = y - static_cast<float>(j0), sy0 = 1.0f - sy1;
    const float sz1 = z - static_cast<float>(k0), sz0 = 1.0f - sz1;

    // Same nesting as Stam 2003 advect(), extended by one level for z.
    return sz0 * (sy0 * (sx0 * f.at(i0, j0, k0) + sx1 * f.at(i1, j0, k0)) +
                  sy1 * (sx0 * f.at(i0, j1, k0) + sx1 * f.at(i1, j1, k0))) +
           sz1 * (sy0 * (sx0 * f.at(i0, j0, k1) + sx1 * f.at(i1, j0, k1)) +
                  sy1 * (sx0 * f.at(i0, j1, k1) + sx1 * f.at(i1, j1, k1)));
}

} // namespace smoke::serial
