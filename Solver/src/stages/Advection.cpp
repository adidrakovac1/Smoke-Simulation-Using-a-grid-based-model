#include "stages/Interpolation.h"
#include "stages/Stages.h"

namespace smoke::serial {

// Semi-Lagrangian advection (Stam 1999, section 2.2 and appendix A; Stam 2003, advect()):
// "where did the fluid now at this cell centre come from dt seconds ago?" Trace back one Euler step,
// clamp to the box, and take the old value there by trilinear interpolation. With h = 1 (decision D4)
// the velocity is in cells per second, so the back-traced position is simply  i - dt * u.
void advectScalar(Field3D& dst, const Field3D& src, const Field3D& u, const Field3D& v, const Field3D& w,
                  float dt, SlabRange range) {
    const GridSize s = dst.size();
    for (int k = range.k0; k < range.k1; ++k) {
        for (int j = 1; j <= s.ny; ++j) {
            for (int i = 1; i <= s.nx; ++i) {
                const float x = clampToBox(static_cast<float>(i) - dt * u.at(i, j, k), s.nx);
                const float y = clampToBox(static_cast<float>(j) - dt * v.at(i, j, k), s.ny);
                const float z = clampToBox(static_cast<float>(k) - dt * w.at(i, j, k), s.nz);
                dst.at(i, j, k) = sampleTrilinear(src, x, y, z);
            }
        }
    }
}

// Self-advection of the velocity (Stam 1999, section 2.2): each component is a field like any other and is
// carried by the flow. All three components are traced back with the OLD velocity (u0, v0, w0), so the
// result does not depend on the order in which components or cells are processed.
void advectVelocity(SmokeState& s, float dt, SlabRange range) {
    s.u.swap(s.u0);
    s.v.swap(s.v0);
    s.w.swap(s.w0);
    advectScalar(s.u, s.u0, s.u0, s.v0, s.w0, dt, range);
    advectScalar(s.v, s.v0, s.u0, s.v0, s.w0, dt, range);
    advectScalar(s.w, s.w0, s.u0, s.v0, s.w0, dt, range);
    setVelocityBoundary(s);
}

void advectScalars(SmokeState& s, float dt, SlabRange range) {
    // Double buffering: after the swap, density0 holds the old values and density is overwritten.
    s.density.swap(s.density0);
    advectScalar(s.density, s.density0, s.u, s.v, s.w, dt, range);
    setBoundary(s.density, BoundaryKind::Scalar);

    s.temperature.swap(s.temperature0);
    advectScalar(s.temperature, s.temperature0, s.u, s.v, s.w, dt, range);
    setBoundary(s.temperature, BoundaryKind::Scalar);
}

} // namespace smoke::serial
