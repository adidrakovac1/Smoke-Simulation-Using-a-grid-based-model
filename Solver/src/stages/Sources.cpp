#include "stages/Stages.h"

#include <algorithm>
#include <cmath>

namespace smoke::serial {

// Like add_source in Stam 2003 (x += dt * s), but the source "s" is a sphere given by params.source
// instead of a user-filled array (docs/plan/MATH.md, section 3).
void addSources(SmokeState& state, const SmokeParams& params, float dt, SlabRange range) {
    // As before D19: nothing to do (and no setBoundary) when no source has a positive radius.
    const float nx       = static_cast<float>(state.density.size().nx);
    bool        anySource = params.source.radius * nx > 0.0f;
    for (int n = 0; n < params.extraSourceCount && n < kMaxSources - 1; ++n)
        anySource = anySource || params.extraSources[static_cast<std::size_t>(n)].radius * nx > 0.0f;
    if (!anySource) return;
    addSourcesSlab(state, params, dt, range);
    setBoundary(state.density, BoundaryKind::Scalar);
    setBoundary(state.temperature, BoundaryKind::Scalar);
}

// The interior part of addSources for the planes of `range`; the caller fills the ghost layer. Returns the
// planes [k0, k1) it may have changed (empty when the sphere misses the slab). D19: SmokeSolver::step refreshes
// the ghost layer only for those planes. That gives the same result as setBoundary on the whole field, because
// the ghost cells of every other plane already hold the values setBoundary would write: every phase that
// changes density or temperature ends with setBoundary (Stages.h invariant), so the ghost layer is always a
// copy of the current interior.
namespace {

// One source sphere for the planes of `range`; returns the planes it may have changed (empty: k0 == k1).
SlabRange addSphereSlab(SmokeState& state, const SmokeSource& src, float dt, SlabRange range) {
    const GridSize g = state.density.size();

    // Relative position -> cell-index coordinates. The box spans [0.5, n + 0.5], so x = 0.5 maps to the
    // exact middle (n + 1) / 2, which keeps a centred source mirror-symmetric (test T6).
    const float cx     = 0.5f + src.x * static_cast<float>(g.nx);
    const float cy     = 0.5f + src.y * static_cast<float>(g.ny);
    const float cz     = 0.5f + src.z * static_cast<float>(g.nz);
    const float radius = src.radius * static_cast<float>(g.nx);
    if (radius <= 0.0f) return {range.k0, range.k0};
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
    return k0 <= k1 ? SlabRange{k0, k1 + 1} : SlabRange{range.k0, range.k0};
}

} // namespace

// D22: the main source, then the extra ones in order (each adds to what is already there, so the order does not
// change the result). The returned range is the union of the planes every sphere may have changed; the planes
// between two spheres get their ghost layer refreshed too, which writes the values it already holds.
SlabRange addSourcesSlab(SmokeState& state, const SmokeParams& params, float dt, SlabRange range) {
    SlabRange touched = addSphereSlab(state, params.source, dt, range);
    for (int n = 0; n < params.extraSourceCount && n < kMaxSources - 1; ++n) {
        const SlabRange more = addSphereSlab(state, params.extraSources[static_cast<std::size_t>(n)], dt, range);
        if (more.k0 >= more.k1) continue;
        touched = touched.k0 < touched.k1 ? SlabRange{std::min(touched.k0, more.k0), std::max(touched.k1, more.k1)}
                                          : more;
    }
    return touched;
}

} // namespace smoke::serial
