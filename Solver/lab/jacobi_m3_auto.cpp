// Version 2: M3 Jacobi code with the default Release flags (the compiler may auto-vectorize; on x86-64
// without -mavx2 that means SSE, 4 floats). See JacobiM3.inc.
#include "LabKernels.h"

namespace {
#include "JacobiM3.inc"
} // namespace

namespace smoke_lab {
void jacobiM3Auto(smoke::Field3D& pNew, const smoke::Field3D& pOld, const smoke::Field3D& div) {
    jacobiM3(pNew, pOld, div);
}
} // namespace smoke_lab
