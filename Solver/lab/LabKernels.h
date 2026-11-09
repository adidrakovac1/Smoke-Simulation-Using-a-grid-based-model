#pragma once
// Kernel versions that exist ONLY in smoke_lab (history kept for the side-by-side comparison).
// The newer versions are the solver's own functions (smoke::serial::jacobiIteration and advectScalar with
// KernelIsa), so smoke_lab always measures the code the simulation really runs.
#include "smoke/Field3D.h"

namespace smoke_lab {

void jacobiM3Scalar(smoke::Field3D& pNew, const smoke::Field3D& pOld, const smoke::Field3D& div);
void jacobiM3Auto(smoke::Field3D& pNew, const smoke::Field3D& pOld, const smoke::Field3D& div);

void advectM3Scalar(smoke::Field3D& dst, const smoke::Field3D& src, const smoke::Field3D& u, const smoke::Field3D& v,
                    const smoke::Field3D& w, float dt);
void advectM3Auto(smoke::Field3D& dst, const smoke::Field3D& src, const smoke::Field3D& u, const smoke::Field3D& v,
                  const smoke::Field3D& w, float dt);

} // namespace smoke_lab
