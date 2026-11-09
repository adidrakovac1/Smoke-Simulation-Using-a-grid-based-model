// Version 1: M3 advection code, compiled WITHOUT vectorization (CMake adds -fno-tree-vectorize to this file
// only, like the *_scalar.cpp files of the lab example). See AdvectionM3.inc.
#include "LabKernels.h"

namespace {
#include "AdvectionM3.inc"
} // namespace

namespace smoke_lab {
void advectM3Scalar(smoke::Field3D& dst, const smoke::Field3D& src, const smoke::Field3D& u, const smoke::Field3D& v,
                    const smoke::Field3D& w, float dt) {
    advectM3(dst, src, u, v, w, dt);
}
} // namespace smoke_lab
