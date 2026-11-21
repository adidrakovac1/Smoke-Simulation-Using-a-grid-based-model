#include "stages/Stages.h"

#include "CpuFeatures.h"
#include "Restrict.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>

namespace smoke::serial {

// Buoyancy, Fedkiw et al. 2001, eq. (8):  f = ( -alpha * rho + beta * (T - T_amb) ) * up.
// Heavy smoke is pulled down (alpha), hot gas is pushed up (beta). In our box "up" is +y
// (docs/plan/ARCHITECTURE.md, section 8), so only v changes. The force is added as v += dt * f
// (Fedkiw et al. 2001, section 4; Stam 1999, addForce). Forces are cell-centred like everything else.
void applyBuoyancy(SmokeState& s, const SmokeParams& p, float dt, SlabRange range) {
    applyBuoyancySlab(s, p, dt, range);
    setBoundary(s.v, BoundaryKind::VelocityY);
}

// The slab kernel deliberately leaves ghost cells alone. Parallel callers apply the boundary
// once, after every worker has finished writing its interior cells.
void applyBuoyancySlab(SmokeState& s, const SmokeParams& p, float dt, SlabRange range) {
    const GridSize g     = s.v.size();
    const float    alpha = p.buoyancyAlpha;
    const float    beta  = p.buoyancyBeta;
    const float    tAmb  = p.ambientTemp;
    for (int k = range.k0; k < range.k1; ++k) {
        for (int j = 1; j <= g.ny; ++j) {
            for (int i = 1; i <= g.nx; ++i) {
                const float force = -alpha * s.density.at(i, j, k) + beta * (s.temperature.at(i, j, k) - tAmb);
                s.v.at(i, j, k) += dt * force;
            }
        }
    }
}

// D22: fans (docs/plan/MATH.md, section 3, "Ventilator"). Each fan pushes the cells inside a cylinder in front of
// it along its axis: u += dt * strength * (1 - a / reach) * n, where a is the distance along the axis. Only the
// bounding box of the cylinder is visited, clipped to this slab. Fans are added one after another in their order,
// the same in every backend, so Serial and CpuParallel stay bit-identical.
bool anyFanActive(const SmokeParams& p) {
    for (int f = 0; f < p.fanCount && f < kMaxFans; ++f) {
        const SmokeFan& fan = p.fans[static_cast<std::size_t>(f)];
        if (fan.strength != 0.0f && fan.radius > 0.0f && fan.reach > 0.0f &&
            (fan.dirX != 0.0f || fan.dirY != 0.0f || fan.dirZ != 0.0f))
            return true;
    }
    return false;
}

void applyFansSlab(SmokeState& s, const SmokeParams& p, float dt, SlabRange range) {
    const GridSize g = s.u.size();
    for (int f = 0; f < p.fanCount && f < kMaxFans; ++f) {
        const SmokeFan& fan = p.fans[static_cast<std::size_t>(f)];
        const float     len = std::sqrt(fan.dirX * fan.dirX + fan.dirY * fan.dirY + fan.dirZ * fan.dirZ);
        if (fan.strength == 0.0f || len == 0.0f) continue;
        const float nxD = fan.dirX / len, nyD = fan.dirY / len, nzD = fan.dirZ / len;
        // Relative position -> cell-index coordinates, exactly like the source sphere (Sources.cpp).
        const float cx     = 0.5f + fan.x * static_cast<float>(g.nx);
        const float cy     = 0.5f + fan.y * static_cast<float>(g.ny);
        const float cz     = 0.5f + fan.z * static_cast<float>(g.nz);
        const float radius = fan.radius * static_cast<float>(g.nx);
        const float reach  = fan.reach * static_cast<float>(g.nx);
        if (radius <= 0.0f || reach <= 0.0f) continue;
        const float r2 = radius * radius;

        // Bounding box of the cylinder: both end centres, widened by the radius.
        const float ex = cx + reach * nxD, ey = cy + reach * nyD, ez = cz + reach * nzD;
        const int   i0 = std::max(1, static_cast<int>(std::floor(std::min(cx, ex) - radius)));
        const int   i1 = std::min(g.nx, static_cast<int>(std::ceil(std::max(cx, ex) + radius)));
        const int   j0 = std::max(1, static_cast<int>(std::floor(std::min(cy, ey) - radius)));
        const int   j1 = std::min(g.ny, static_cast<int>(std::ceil(std::max(cy, ey) + radius)));
        const int   k0 = std::max(range.k0, static_cast<int>(std::floor(std::min(cz, ez) - radius)));
        const int   k1 = std::min(range.k1 - 1, static_cast<int>(std::ceil(std::max(cz, ez) + radius)));

        const float push = dt * fan.strength;
        for (int k = k0; k <= k1; ++k) {
            for (int j = j0; j <= j1; ++j) {
                for (int i = i0; i <= i1; ++i) {
                    const float dx = static_cast<float>(i) - cx;
                    const float dy = static_cast<float>(j) - cy;
                    const float dz = static_cast<float>(k) - cz;
                    const float a  = dx * nxD + dy * nyD + dz * nzD;
                    if (a < 0.0f || a > reach) continue;
                    if (dx * dx + dy * dy + dz * dz - a * a > r2) continue;
                    const float amount = push * (1.0f - a / reach);
                    s.u.at(i, j, k) += amount * nxD;
                    s.v.at(i, j, k) += amount * nyD;
                    s.w.at(i, j, k) += amount * nzD;
                }
            }
        }
    }
}

void applyForces(SmokeState& state, const SmokeParams& params, float dt, SlabRange range) {
    applyBuoyancy(state, params, dt, range);
    if (anyFanActive(params)) {
        applyFansSlab(state, params, dt, range);
        setBoundary(state.u, BoundaryKind::VelocityX);
        setBoundary(state.v, BoundaryKind::VelocityY);
        setBoundary(state.w, BoundaryKind::VelocityZ);
    }
    applyVorticityConfinement(state, params.vorticityEps, dt, range);
}

namespace {
// vorticityRow / confinementRow (M4-019): the same source is compiled again with -mavx2 in Forces_avx2.cpp.
#include "stages/ForcesRows.inc"
} // namespace

#if defined(SMOKE_HAS_AVX2_BINARY)
// Defined in Forces_avx2.cpp (compiled with -mavx2). Only call them when avx2Supported() is true.
void vorticitySlabAvx2(float* u, float* v, float* w, float* ox, float* oy, float* oz, float* mag, int nx, int ny,
                       int k0, int k1, std::ptrdiff_t sJ, std::ptrdiff_t sK);
void confinementSlabAvx2(float* u, float* v, float* w, float* ox, float* oy, float* oz, float* mag, int nx, int ny,
                         int k0, int k1, float epsilonDt, std::ptrdiff_t sJ, std::ptrdiff_t sK);
void vorticityMagnitudeSlabAvx2(float* mag, const float* u, const float* v, const float* w, int nx, int ny, int k0,
                                int k1, std::ptrdiff_t sJ, std::ptrdiff_t sK);
void confinementOutSlabAvx2(float* uOut, float* vOut, float* wOut, const float* u, const float* v, const float* w,
                            const float* mag, int nx, int ny, int k0, int k1, float epsilonDt, std::ptrdiff_t sJ,
                            std::ptrdiff_t sK);
#endif

// Vorticity confinement (Fedkiw et al. 2001, eq. 9-11; docs/plan/MATH.md, section 3):
//   pass 1: w = curl u and |w| for every interior cell (central differences, h = 1);
//   pass 2: N = grad|w| / |grad|w||,  u += eps * dt * (N x w).
// |w| needs its ghost layer between the passes (setBoundary), because pass 2 reads its neighbours.
// The kernels are in ForcesRows.inc (M4-019); `isa` selects the build as for Jacobi (KernelIsa, M4-020).
bool applyVorticityConfinement(SmokeState& s, float epsilon, float dt, SlabRange range, KernelIsa isa) {
    const bool haveAvx2 = avx2Supported();  // always false when the AVX2 file is not built
    if (isa == KernelIsa::Avx2Intrinsics) return false;  // there is no hand-written version
    if (isa == KernelIsa::Avx2 && !haveAvx2) return false;
    if (!(epsilon > 0.0f)) return true;

    const GridSize g = s.u.size();
    const std::ptrdiff_t sJ = s.u.strideJ(), sK = s.u.strideK();
    assert(s.v.strideK() == sK && s.w.strideK() == sK && s.vorticityMagnitude.strideK() == sK &&
           s.vorticityX.strideK() == sK && s.vorticityY.strideK() == sK && s.vorticityZ.strideK() == sK);
    const VorticityFields f{s.u.data(),          s.v.data(),          s.w.data(),
                            s.vorticityX.data(), s.vorticityY.data(), s.vorticityZ.data(),
                            s.vorticityMagnitude.data()};
    const float epsilonDt = epsilon * dt;

#if defined(SMOKE_HAS_AVX2_BINARY)
    if (isa != KernelIsa::Scalar && haveAvx2) {  // M4-020
        vorticitySlabAvx2(f.u, f.v, f.w, f.ox, f.oy, f.oz, f.mag, g.nx, g.ny, range.k0, range.k1, sJ, sK);
        setBoundary(s.vorticityMagnitude, BoundaryKind::Scalar);
        confinementSlabAvx2(f.u, f.v, f.w, f.ox, f.oy, f.oz, f.mag, g.nx, g.ny, range.k0, range.k1, epsilonDt, sJ,
                            sK);
        setVelocityBoundary(s);
        return true;
    }
#endif
    vorticitySlab(f, g.nx, g.ny, range.k0, range.k1, sJ, sK);
    setBoundary(s.vorticityMagnitude, BoundaryKind::Scalar);
    confinementSlab(f, g.nx, g.ny, range.k0, range.k1, epsilonDt, sJ, sK);
    setVelocityBoundary(s);
    return true;
}

// D19: the two passes of applyVorticityConfinement as separate slab calls, for the parallel step, with fewer
// bytes through memory (ForcesRows.inc, vorticityMagnitudeRow / confinementOutRow). Pass 1 of a slab reads
// u, v, w of the neighbouring planes (other slabs) and writes only |w| of its own planes; pass 2 reads u, v, w
// and |w| of the neighbouring planes and writes the new velocity of its own planes into s.u0, s.v0, s.w0 (u, v,
// w stay unchanged). So every slab can run in parallel as long as all of pass 1 (and the ghost layer of |w|)
// is finished before pass 2 starts. The vorticity components themselves are no longer stored.
// Same arithmetic and the same KernelIsa::Auto choice as applyVorticityConfinement: bit-identical.
void vorticityPassSlab(SmokeState& s, SlabRange range) {
    const GridSize g = s.u.size();
    const std::ptrdiff_t sJ = s.u.strideJ(), sK = s.u.strideK();
    assert(s.vorticityMagnitude.strideK() == sK);
#if defined(SMOKE_HAS_AVX2_BINARY)
    if (avx2Supported()) {
        vorticityMagnitudeSlabAvx2(s.vorticityMagnitude.data(), s.u.data(), s.v.data(), s.w.data(), g.nx, g.ny,
                                   range.k0, range.k1, sJ, sK);
        return;
    }
#endif
    vorticityMagnitudeSlab(s.vorticityMagnitude.data(), s.u.data(), s.v.data(), s.w.data(), g.nx, g.ny, range.k0,
                           range.k1, sJ, sK);
}

void confinementPassSlab(SmokeState& s, float epsilonDt, SlabRange range) {
    const GridSize g = s.u.size();
    const std::ptrdiff_t sJ = s.u.strideJ(), sK = s.u.strideK();
    assert(s.u0.strideK() == sK && s.v0.strideK() == sK && s.w0.strideK() == sK);
#if defined(SMOKE_HAS_AVX2_BINARY)
    if (avx2Supported()) {
        confinementOutSlabAvx2(s.u0.data(), s.v0.data(), s.w0.data(), s.u.data(), s.v.data(), s.w.data(),
                               s.vorticityMagnitude.data(), g.nx, g.ny, range.k0, range.k1, epsilonDt, sJ, sK);
        return;
    }
#endif
    confinementOutSlab(s.u0.data(), s.v0.data(), s.w0.data(), s.u.data(), s.v.data(), s.w.data(),
                       s.vorticityMagnitude.data(), g.nx, g.ny, range.k0, range.k1, epsilonDt, sJ, sK);
}

} // namespace smoke::serial
