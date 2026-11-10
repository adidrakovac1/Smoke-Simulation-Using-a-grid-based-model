#include "stages/Stages.h"

#include <algorithm>
#include <cmath>

namespace smoke::serial {

// Like add_source in Stam 2003 (x += dt * s), but the source "s" is a sphere given by params.source
// instead of a user-filled array (docs/plan/MATH.md, section 3).
void addSources(SmokeState& state, const SmokeParams& params, float dt, SlabRange range) {
    const SmokeSource& src = params.source;
    const GridSize     g   = state.density.size();

    // Relative position -> cell-index coordinates. The box spans [0.5, n + 0.5], so x = 0.5 maps to the
    // exact middle (n + 1) / 2, which keeps a centred source mirror-symmetric (test T6).
    const float cx     = 0.5f + src.x * static_cast<float>(g.nx);
    const float cy     = 0.5f + src.y * static_cast<float>(g.ny);
    const float cz     = 0.5f + src.z * static_cast<float>(g.nz);
    const float radius = src.radius * static_cast<float>(g.nx);
    if (radius <= 0.0f) return;
    const float r2 = radius * radius;

    // Only visit the bounding box of the sphere (clipped to the interior and to this call's slab).
    const int i0 = std::max(1, static_cast<int>(std::floor(cx - radius)));
    const int i1 = std::min(g.nx, static_cast<int>(std::ceil(cx + radius)));
    const int j0 = std::max(1, static_cast<int>(std::floor(cy - radius)));
    const int j1 = std::min(g.ny, static_cast<int>(std::ceil(cy + radius)));
    const int k0 = std::max(range.k0, static_cast<int>(std::floor(cz - radius)));
    const int k1 = std::min(range.k1 - 1, static_cast<int>(std::ceil(cz + radius)));

    const float addDensity     = dt * src.densityRate;
    const float addTemperature = dt * src.temperatureRate;

    for (int k = k0; k <= k1; ++k) {
        for (int j = j0; j <= j1; ++j) {
            for (int i = i0; i <= i1; ++i) {
                const float dx = static_cast<float>(i) - cx;
                const float dy = static_cast<float>(j) - cy;
                const float dz = static_cast<float>(k) - cz;
                if (dx * dx + dy * dy + dz * dz <= r2) {
                    state.density.at(i, j, k)     += addDensity;
                    state.temperature.at(i, j, k) += addTemperature;
                }
            }
        }
    }

    setBoundary(state.density, BoundaryKind::Scalar);
    setBoundary(state.temperature, BoundaryKind::Scalar);
}

} // namespace smoke::serial
