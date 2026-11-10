// AoS ("array of structures") version of the M3 advection, for the SoA vs AoS comparison in smoke_lab and
// smoke_demo. The solver stores every quantity in its own array (SoA, "structure of arrays", decision of M0,
// docs/plan/ARCHITECTURE.md); here all 5 quantities of a cell sit next to each other in one struct, as an
// "obvious" first design would do. Same M3 algorithm and arithmetic (5 passes, one per quantity), so the result
// is bit-identical; only the memory layout differs. Compiled with the default Release flags (compare with the
// M3 SoA version with the same flags).
//
// Why AoS is slower here: one pass needs one quantity of 8 neighbouring cells, but every cache line now also
// carries the 4 other quantities, so about 5x more bytes are moved per useful float, and the compiler cannot
// load consecutive cells of one quantity with one vector load.
#include "LabKernels.h"

namespace {
#include "AdvectionM3.inc"  // clampToBoxM3 (the M3 clamp)

float sampleTrilinearAoS(const smoke_lab::CellAoS* f, int q, std::ptrdiff_t sJ, std::ptrdiff_t sK, float x, float y,
                         float z) {
    const int i0 = static_cast<int>(x);
    const int j0 = static_cast<int>(y);
    const int k0 = static_cast<int>(z);
    const int i1 = i0 + 1;
    const int j1 = j0 + 1;
    const int k1 = k0 + 1;
    const float sx1 = x - static_cast<float>(i0), sx0 = 1.0f - sx1;
    const float sy1 = y - static_cast<float>(j0), sy0 = 1.0f - sy1;
    const float sz1 = z - static_cast<float>(k0), sz0 = 1.0f - sz1;
    auto at = [&](int i, int j, int k) { return f[i + j * sJ + k * sK].q[q]; };
    return sz0 * (sy0 * (sx0 * at(i0, j0, k0) + sx1 * at(i1, j0, k0)) +
                  sy1 * (sx0 * at(i0, j1, k0) + sx1 * at(i1, j1, k0))) +
           sz1 * (sy0 * (sx0 * at(i0, j0, k1) + sx1 * at(i1, j0, k1)) +
                  sy1 * (sx0 * at(i0, j1, k1) + sx1 * at(i1, j1, k1)));
}
} // namespace

namespace smoke_lab {
void advectM3AoS(CellAoS* dst, const CellAoS* src, const smoke::GridSize s, std::ptrdiff_t sJ, std::ptrdiff_t sK,
                 float dt) {
    for (int q = 0; q < kAoSQuantities; ++q)  // one pass per quantity, as the 5 advectScalar calls of M3
        for (int k = 1; k <= s.nz; ++k)
            for (int j = 1; j <= s.ny; ++j)
                for (int i = 1; i <= s.nx; ++i) {
                    const CellAoS& c = src[i + j * sJ + k * sK];  // velocity = q[0], q[1], q[2]
                    const float x = clampToBoxM3(static_cast<float>(i) - dt * c.q[0], s.nx);
                    const float y = clampToBoxM3(static_cast<float>(j) - dt * c.q[1], s.ny);
                    const float z = clampToBoxM3(static_cast<float>(k) - dt * c.q[2], s.nz);
                    dst[i + j * sJ + k * sK].q[q] = sampleTrilinearAoS(src, q, sJ, sK, x, y, z);
                }
}
} // namespace smoke_lab
