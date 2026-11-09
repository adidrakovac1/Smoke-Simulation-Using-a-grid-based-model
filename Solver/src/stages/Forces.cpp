#include "stages/Stages.h"

#include "CpuFeatures.h"
#include "Restrict.h"

#include <cassert>
#include <cmath>
#include <cstddef>

namespace smoke::serial {

// Buoyancy, Fedkiw et al. 2001, eq. (8):  f = ( -alpha * rho + beta * (T - T_amb) ) * up.
// Heavy smoke is pulled down (alpha), hot gas is pushed up (beta). In our box "up" is +y
// (docs/plan/ARCHITECTURE.md, section 8), so only v changes. The force is added as v += dt * f
// (Fedkiw et al. 2001, section 4; Stam 1999, addForce). Forces are cell-centred like everything else.
void applyBuoyancy(SmokeState& s, const SmokeParams& p, float dt, SlabRange range) {
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
    setBoundary(s.v, BoundaryKind::VelocityY);
}

void applyForces(SmokeState& state, const SmokeParams& params, float dt, SlabRange range) {
    applyBuoyancy(state, params, dt, range);
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

} // namespace smoke::serial
