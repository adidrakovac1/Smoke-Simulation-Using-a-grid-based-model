#include "stages/Stages.h"

namespace smoke::serial {

void advectVelocity(SmokeState& /*state*/, float /*dt*/, SlabRange /*range*/) {
    // TODO(M2): semi-Lagrangian advection of u, v, w by the old velocity field (Stam 1999).
    // Read from *0 fields, write into the main fields, then setBoundary.
}

void advectScalars(SmokeState& /*state*/, float /*dt*/, SlabRange /*range*/) {
    // TODO(M1): semi-Lagrangian advection of density and temperature:
    //   rho_new(x) = rho_old(x - dt * u(x)), trilinear interpolation, clamp inside the box.
}

} // namespace smoke::serial
