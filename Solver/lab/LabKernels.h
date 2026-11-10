#pragma once
// Kernel versions that exist ONLY in smoke_lab / smoke_demo (history and demonstrations for the side-by-side
// comparison). The newer versions are the solver's own functions (smoke::serial::jacobiIteration and
// advectScalar / advectFields with KernelIsa), so the programs always measure the code the simulation runs.
#include "smoke/Field3D.h"

#include <cstddef>

namespace smoke_lab {

void jacobiM3Scalar(smoke::Field3D& pNew, const smoke::Field3D& pOld, const smoke::Field3D& div);
void jacobiM3Auto(smoke::Field3D& pNew, const smoke::Field3D& pOld, const smoke::Field3D& div);
// Lab technique T3: M3 Jacobi with the i loop unrolled 4x, compiled without vectorization.
void jacobiM3ScalarUnrolled(smoke::Field3D& pNew, const smoke::Field3D& pOld, const smoke::Field3D& div);

void advectM3Scalar(smoke::Field3D& dst, const smoke::Field3D& src, const smoke::Field3D& u, const smoke::Field3D& v,
                    const smoke::Field3D& w, float dt);
void advectM3Auto(smoke::Field3D& dst, const smoke::Field3D& src, const smoke::Field3D& u, const smoke::Field3D& v,
                  const smoke::Field3D& w, float dt);

// AoS layout for the SoA vs AoS comparison: all quantities of one cell in one struct, in the order
// u, v, w, density, temperature. Same cell indexing (strides) as Field3D.
inline constexpr int kAoSQuantities = 5;
struct CellAoS {
    float q[kAoSQuantities];
};
// M3 advection of all 5 quantities (5 passes) on the AoS array; velocity = q[0], q[1], q[2] of `src`.
void advectM3AoS(CellAoS* dst, const CellAoS* src, smoke::GridSize s, std::ptrdiff_t sJ, std::ptrdiff_t sK, float dt);

} // namespace smoke_lab
