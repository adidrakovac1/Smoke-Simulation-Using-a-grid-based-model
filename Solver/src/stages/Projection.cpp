#include "stages/Stages.h"

namespace smoke::serial {

void project(SmokeState& /*state*/, const SmokeParams& /*params*/, SlabRange /*range*/) {
    // TODO(M2):
    //   1. divergence of the velocity field
    //   2. params.jacobiIterations Jacobi iterations for pressure (read old pressure, write new, swap)
    //   3. subtract the pressure gradient from the velocity
    // Record max |div| before and after for Diagnostics (see docs/plan/MATH.md, section 5).
}

} // namespace smoke::serial
