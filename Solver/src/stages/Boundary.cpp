#include "stages/Stages.h"

namespace smoke::serial {

void setBoundary(Field3D& /*field*/, BoundaryKind /*kind*/) {
    // TODO(M1): copy the nearest interior value into the ghost layer for scalars;
    // for velocity, negate the component normal to the wall (see Stam 2003, set_bnd).
}

} // namespace smoke::serial
