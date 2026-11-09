#include "stages/Stages.h"

#include <cmath>

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

void applyVorticityConfinement(SmokeState& s, float epsilon, float dt, SlabRange range) {
    if (!(epsilon > 0.0f)) return;

    const GridSize g = s.u.size();
    for (int k = range.k0; k < range.k1; ++k) {
        for (int j = 1; j <= g.ny; ++j) {
            for (int i = 1; i <= g.nx; ++i) {
                const float wx = 0.5f * ((s.w.at(i, j + 1, k) - s.w.at(i, j - 1, k)) -
                                         (s.v.at(i, j, k + 1) - s.v.at(i, j, k - 1)));
                const float wy = 0.5f * ((s.u.at(i, j, k + 1) - s.u.at(i, j, k - 1)) -
                                         (s.w.at(i + 1, j, k) - s.w.at(i - 1, j, k)));
                const float wz = 0.5f * ((s.v.at(i + 1, j, k) - s.v.at(i - 1, j, k)) -
                                         (s.u.at(i, j + 1, k) - s.u.at(i, j - 1, k)));
                s.vorticityX.at(i, j, k) = wx;
                s.vorticityY.at(i, j, k) = wy;
                s.vorticityZ.at(i, j, k) = wz;
                s.vorticityMagnitude.at(i, j, k) = std::sqrt(wx * wx + wy * wy + wz * wz);
            }
        }
    }
    setBoundary(s.vorticityMagnitude, BoundaryKind::Scalar);

    for (int k = range.k0; k < range.k1; ++k) {
        for (int j = 1; j <= g.ny; ++j) {
            for (int i = 1; i <= g.nx; ++i) {
                const float nx = 0.5f * (s.vorticityMagnitude.at(i + 1, j, k) -
                                         s.vorticityMagnitude.at(i - 1, j, k));
                const float ny = 0.5f * (s.vorticityMagnitude.at(i, j + 1, k) -
                                         s.vorticityMagnitude.at(i, j - 1, k));
                const float nz = 0.5f * (s.vorticityMagnitude.at(i, j, k + 1) -
                                         s.vorticityMagnitude.at(i, j, k - 1));
                const float gradientMagnitude = std::sqrt(nx * nx + ny * ny + nz * nz);
                const bool hasGradient = gradientMagnitude > 0.0f;
                // Use a finite denominator so the zero-gradient case can be selected without a branch.
                const float scale = epsilon * dt / (hasGradient ? gradientMagnitude : 1.0f);  // h = 1 cell
                const float wx = s.vorticityX.at(i, j, k);
                const float wy = s.vorticityY.at(i, j, k);
                const float wz = s.vorticityZ.at(i, j, k);
                const float forceX = scale * (ny * wz - nz * wy);
                const float forceY = scale * (nz * wx - nx * wz);
                const float forceZ = scale * (nx * wy - ny * wx);
                const float u = s.u.at(i, j, k);
                const float v = s.v.at(i, j, k);
                const float w = s.w.at(i, j, k);
                s.u.at(i, j, k) = hasGradient ? u + forceX : u;
                s.v.at(i, j, k) = hasGradient ? v + forceY : v;
                s.w.at(i, j, k) = hasGradient ? w + forceZ : w;
            }
        }
    }
    setVelocityBoundary(s);
}

} // namespace smoke::serial
