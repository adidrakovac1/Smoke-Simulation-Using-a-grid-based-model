#include "stages/Stages.h"

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
    // TODO(M3): vorticity confinement (eps = 0 means off)   (Fedkiw et al. 2001, eq. 9-11)
}

} // namespace smoke::serial
