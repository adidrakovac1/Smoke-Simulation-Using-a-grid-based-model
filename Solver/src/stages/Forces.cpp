#include "stages/Stages.h"

namespace smoke::serial {

void applyForces(SmokeState& /*state*/, const SmokeParams& /*params*/, float /*dt*/, SlabRange /*range*/) {
    // TODO(M2): buoyancy  f = (-alpha * rho + beta * (T - T_amb)) * up   (Fedkiw et al. 2001, eq. 8)
    // TODO(M3): vorticity confinement (eps = 0 means off)               (Fedkiw et al. 2001, eq. 9-11)
}

} // namespace smoke::serial
