#include "stages/Stages.h"

#include "Restrict.h"

#include <cstddef>

namespace smoke::serial {

// Stam 2003 (set_bnd) in 3D. The ghost layer is filled one axis at a time:
//   1. x walls (i = 0 and nx+1) for the interior rows j in [1,ny], k in [1,nz];
//   2. y walls (j = 0 and ny+1) for i in [0, nx+1], i.e. INCLUDING the ghost columns written in step 1;
//   3. z walls (k = 0 and nz+1) for i in [0, nx+1], j in [0, ny+1], i.e. including everything above.
// Because each pass also covers the ghost cells of the previous passes, the edges and corners of the ghost
// layer get a value by the same rule (copy or sign flip) as the faces. Stam's 2D code averages the corners
// instead; this deliberate difference is documented in docs/plan/MATH.md, section 6.
//
// D19 (slabs): passes 1 and 2 for plane k read and write only plane k, and pass 3 reads only planes 1 and nz
// after passes 1 and 2 are done for them. So the work splits by planes: setBoundarySlab(f, kind, {k0, k1})
// does passes 1 and 2 for the planes k0..k1-1, and pass 3 for the ghost plane next to plane 1 (or nz) when
// that plane is in the slab. Slabs that together cover [1, nz] give exactly the result of setBoundary, in any
// order and in parallel, because no two slabs touch the same cell. The copies are plain loads and stores
// (and a multiplication by +-1, which is exact), so the result is bit-identical.
namespace {

// dst[i] = sign * src[i] for i in [0, n): one row of a ghost face. Contiguous, so it vectorizes.
inline void copyRow(float* SMOKE_RESTRICT dst, const float* SMOKE_RESTRICT src, int n, float sign) {
#pragma omp simd
    for (int i = 0; i < n; ++i) dst[i] = sign * src[i];
}

} // namespace

void setBoundarySlab(Field3D& f, BoundaryKind kind, SlabRange range) {
    const GridSize s = f.size();
    const int nx = s.nx, ny = s.ny, nz = s.nz;
    const std::ptrdiff_t sJ = f.strideJ(), sK = f.strideK();
    float* const data = f.data();

    // -1 on the walls normal to the velocity component (no flow through the wall), +1 otherwise.
    const float signX = (kind == BoundaryKind::VelocityX) ? -1.0f : 1.0f;
    const float signY = (kind == BoundaryKind::VelocityY) ? -1.0f : 1.0f;
    const float signZ = (kind == BoundaryKind::VelocityZ) ? -1.0f : 1.0f;

    const int rowLength = nx + 2;  // i in [0, nx + 1]
    for (int k = range.k0; k < range.k1; ++k) {
        float* const plane = data + k * sK;
        for (int j = 1; j <= ny; ++j) {                                     // pass 1: x walls
            float* const row = plane + j * sJ;
            row[0]      = signX * row[1];
            row[nx + 1] = signX * row[nx];
        }
        copyRow(plane, plane + sJ, rowLength, signY);                      // pass 2: y walls
        copyRow(plane + (ny + 1) * sJ, plane + ny * sJ, rowLength, signY);
    }
    // Pass 3: z walls, whole planes including the ghost rows and columns written above.
    if (range.k0 <= 1 && 1 < range.k1)
        for (int j = 0; j <= ny + 1; ++j) copyRow(data + j * sJ, data + sK + j * sJ, rowLength, signZ);
    if (range.k0 <= nz && nz < range.k1)
        for (int j = 0; j <= ny + 1; ++j)
            copyRow(data + (nz + 1) * sK + j * sJ, data + nz * sK + j * sJ, rowLength, signZ);
}

void setBoundary(Field3D& f, BoundaryKind kind) { setBoundarySlab(f, kind, wholeInterior(f.size())); }

void setVelocityBoundary(SmokeState& state) {
    setBoundary(state.u, BoundaryKind::VelocityX);
    setBoundary(state.v, BoundaryKind::VelocityY);
    setBoundary(state.w, BoundaryKind::VelocityZ);
}

void setVelocityBoundarySlab(SmokeState& state, SlabRange range) {
    setBoundarySlab(state.u, BoundaryKind::VelocityX, range);
    setBoundarySlab(state.v, BoundaryKind::VelocityY, range);
    setBoundarySlab(state.w, BoundaryKind::VelocityZ, range);
}

} // namespace smoke::serial
