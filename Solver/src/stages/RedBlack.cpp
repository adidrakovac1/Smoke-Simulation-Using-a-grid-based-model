#include "stages/ObstacleLists.h"
#include "stages/Stages.h"

#include <cstddef>

namespace smoke::serial {

// Red-black Gauss-Seidel / SOR for the pressure equation (decision D20; docs/plan/MATH.md, section 5).
//
// What it is. Jacobi computes every cell from the OLD values of its neighbours, so information moves one cell
// per iteration and two copies of p are needed. Gauss-Seidel uses the NEWEST values instead, which roughly
// halves the number of iterations for the same accuracy, and works in place. Plain Gauss-Seidel goes cell after
// cell and cannot be parallelized or vectorized; the red-black ordering fixes that: colour the grid like a 3D
// chess board. Every neighbour of a red cell is black and vice versa, so all red cells can be updated at the
// same time (they only read black cells), then all black cells (they read the new red values).
// Over-relaxation (SOR): move each cell further than Gauss-Seidel says, p += omega * (gs - p) with 1 < omega < 2;
// for the Poisson equation that converges much faster still (the best omega depends on the grid size).
// Reference: the project() of Stam 2003 updates p in place inside its loop over i, j (lin_solve style), i.e. it
// is Gauss-Seidel in lexicographic order, which cannot be split between threads; red-black is the same
// relaxation in an order that can. TODO(paper: Fedkiw et al. 2001, section 4): which solver they name for p.
//
// The ghost layer. The 7-point stencil of an interior cell reads only face ghost cells, and the face ghost next
// to cell c is a copy of c itself (Scalar rule, dp/dn = 0). So a ghost is read only when its own cell is
// updated, and refreshing the ghost layer once per iteration (after the black sweep) gives every cell the value
// it had before its own update, exactly as in the Jacobi loop.
//
// Threads: z-slabs. The red sweep only reads black cells (also of other slabs), the black sweep only red ones,
// so one barrier after each sweep makes the result independent of the number of threads.
void redBlackSolve(Field3D& p, const Field3D& div, int iterations, float omega, bool warmStart,
                   const ObstacleLists* obstacles, const Team& team) {
    const GridSize g = p.size();
    const std::ptrdiff_t sJ = p.strideJ(), sK = p.strideK();
    const int k0 = 1 + static_cast<int>((static_cast<long long>(team.tid) * g.nz) / team.size);
    const int k1 = 1 + static_cast<int>((static_cast<long long>(team.tid + 1) * g.nz) / team.size);
    const bool mine = k0 < k1;
    float* const data = p.data();

    if (!warmStart) {
        // p = 0 as in project(). Every thread clears its own planes; the first and the last also the ghost planes.
        const int z0 = (k0 == 1) ? 0 : k0;
        const int z1 = (k1 == g.nz + 1) ? g.nz + 2 : k1;
        if (mine)
            for (std::ptrdiff_t c = z0 * sK; c < z1 * sK; ++c) data[c] = 0.0f;
        team.sync();
    }
    if (iterations <= 0) return;

    const RedBlackSweepFn sweep = redBlackSweepKernel();
    const float* const d = div.data();
    for (int it = 0; it < iterations; ++it) {
        if (mine) sweep(data, d, g.nx, g.ny, k0, k1, 0, omega, sJ, sK);  // red
        team.sync();
        if (mine) sweep(data, d, g.nx, g.ny, k0, k1, 1, omega, sJ, sK);  // black
        if (!obstacles) {
            if (mine) setBoundarySlab(p, BoundaryKind::Scalar, {k0, k1});
            team.sync();
        } else {
            // Same order as the Jacobi loop: obstacle surface first, then the ghost layer.
            team.sync();
            if (team.tid == 0) {
                extendIntoSolids(p, *obstacles);
                setBoundary(p, BoundaryKind::Scalar);
            }
            team.sync();
        }
    }
}

} // namespace smoke::serial
