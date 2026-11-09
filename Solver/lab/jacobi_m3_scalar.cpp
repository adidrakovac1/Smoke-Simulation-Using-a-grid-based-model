// Version 1: M3 Jacobi code, compiled WITHOUT vectorization (CMake adds -fno-tree-vectorize to this file only,
// like the *_scalar.cpp files of the lab example). See JacobiM3.inc.
#include "LabKernels.h"

namespace {
#include "JacobiM3.inc"
} // namespace

namespace smoke_lab {
void jacobiM3Scalar(smoke::Field3D& pNew, const smoke::Field3D& pOld, const smoke::Field3D& div) {
    jacobiM3(pNew, pOld, div);
}
} // namespace smoke_lab
