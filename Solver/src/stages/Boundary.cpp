#include "stages/Stages.h"

namespace smoke::serial {

// Stam 2003 (set_bnd) in 3D. The ghost layer is filled one axis at a time:
//   1. x walls (i = 0 and nx+1) for the interior rows j in [1,ny], k in [1,nz];
//   2. y walls (j = 0 and ny+1) for i in [0, nx+1], i.e. INCLUDING the ghost columns written in step 1;
//   3. z walls (k = 0 and nz+1) for i in [0, nx+1], j in [0, ny+1], i.e. including everything above.
// Because each pass also covers the ghost cells of the previous passes, the edges and corners of the ghost
// layer get a value by the same rule (copy or sign flip) as the faces. Stam's 2D code averages the corners
// instead; this deliberate difference is documented in docs/plan/MATH.md, section 6.
void setBoundary(Field3D& f, BoundaryKind kind) {
    const GridSize s = f.size();
    const int nx = s.nx, ny = s.ny, nz = s.nz;

    // -1 on the walls normal to the velocity component (no flow through the wall), +1 otherwise.
    const float signX = (kind == BoundaryKind::VelocityX) ? -1.0f : 1.0f;
    const float signY = (kind == BoundaryKind::VelocityY) ? -1.0f : 1.0f;
    const float signZ = (kind == BoundaryKind::VelocityZ) ? -1.0f : 1.0f;

    for (int k = 1; k <= nz; ++k) {
        for (int j = 1; j <= ny; ++j) {
            f.at(0, j, k)      = signX * f.at(1, j, k);
            f.at(nx + 1, j, k) = signX * f.at(nx, j, k);
        }
    }
    for (int k = 1; k <= nz; ++k) {
        for (int i = 0; i <= nx + 1; ++i) {
            f.at(i, 0, k)      = signY * f.at(i, 1, k);
            f.at(i, ny + 1, k) = signY * f.at(i, ny, k);
        }
    }
    for (int j = 0; j <= ny + 1; ++j) {
        for (int i = 0; i <= nx + 1; ++i) {
            f.at(i, j, 0)      = signZ * f.at(i, j, 1);
            f.at(i, j, nz + 1) = signZ * f.at(i, j, nz);
        }
    }
}

void setVelocityBoundary(SmokeState& state) {
    setBoundary(state.u, BoundaryKind::VelocityX);
    setBoundary(state.v, BoundaryKind::VelocityY);
    setBoundary(state.w, BoundaryKind::VelocityZ);
}

} // namespace smoke::serial
