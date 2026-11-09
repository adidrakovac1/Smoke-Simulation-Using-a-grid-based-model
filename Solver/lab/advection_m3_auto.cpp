// Version 2: M3 advection code with the default Release flags. g++ does NOT vectorize it (branches in the
// clamp, aliasing through Field3D, reads at velocity-dependent addresses; see PERF_LOG.md, M4-011 to M4-013).
#include "LabKernels.h"

namespace {
#include "AdvectionM3.inc"
} // namespace

namespace smoke_lab {
void advectM3Auto(smoke::Field3D& dst, const smoke::Field3D& src, const smoke::Field3D& u, const smoke::Field3D& v,
                  const smoke::Field3D& w, float dt) {
    advectM3(dst, src, u, v, w, dt);
}
} // namespace smoke_lab
