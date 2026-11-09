#include "stages/Interpolation.h"
#include "stages/Stages.h"

#include "CpuFeatures.h"
#include "Restrict.h"

#include <cassert>
#include <climits>
#include <cstddef>

namespace smoke::serial {

// Semi-Lagrangian advection (Stam 1999, section 2.2 and appendix A; Stam 2003, advect()):
// "where did the fluid now at this cell centre come from dt seconds ago?" Trace back one Euler step,
// clamp to the box, and take the old value there by trilinear interpolation. With h = 1 (decision D4)
// the velocity is in cells per second, so the back-traced position is simply  i - dt * u.
namespace {
// advectRow* / advectSlab: the same source is compiled again with -mavx2 in Advection_avx2.cpp (M4-014).
#include "stages/AdvectionRows.inc"
} // namespace

#if defined(SMOKE_HAS_AVX2_BINARY)
// Defined in Advection_avx2.cpp (compiled with -mavx2). Only call it when avx2Supported() is true.
void advectSlabAvx2(float* const* dst, const float* const* src, int count, const float* u, const float* v,
                    const float* w, const GridSize s, int k0, int k1, float dt, std::ptrdiff_t sJ, std::ptrdiff_t sK);
#endif

bool advectFields(Field3D* const dst[], const Field3D* const src[], int count, const Field3D& u, const Field3D& v,
                  const Field3D& w, float dt, SlabRange range, KernelIsa isa) {
    assert(count >= 1 && count <= 3);
    const GridSize s = u.size();
    const std::ptrdiff_t sJ = u.strideJ(), sK = u.strideK();
    assert(v.strideK() == sK && w.strideK() == sK);
    float* d[3] = {};
    const float* f[3] = {};
    for (int n = 0; n < count; ++n) {
        assert(dst[n]->strideK() == sK && src[n]->strideK() == sK);
        assert(src[n]->totalFloats() < static_cast<std::size_t>(INT_MAX));  // 32-bit cell index (Interpolation.h)
        d[n] = dst[n]->data();
        f[n] = src[n]->data();
    }
    const bool haveAvx2 = avx2Supported();  // always false when the AVX2 file is not built
    if (isa == KernelIsa::Avx2Intrinsics) return false;  // there is no hand-written advection version
    if (isa == KernelIsa::Avx2 && !haveAvx2) return false;
#if defined(SMOKE_HAS_AVX2_BINARY)
    if (isa != KernelIsa::Scalar && haveAvx2) {
        advectSlabAvx2(d, f, count, u.data(), v.data(), w.data(), s, range.k0, range.k1, dt, sJ, sK);
        return true;
    }
#endif
    advectSlab(d, f, count, u.data(), v.data(), w.data(), s, range.k0, range.k1, dt, sJ, sK);
    return true;
}

bool advectScalar(Field3D& dst, const Field3D& src, const Field3D& u, const Field3D& v, const Field3D& w,
                  float dt, SlabRange range, KernelIsa isa) {
    Field3D* const d[1] = {&dst};
    const Field3D* const f[1] = {&src};
    return advectFields(d, f, 1, u, v, w, dt, range, isa);
}

// Self-advection of the velocity (Stam 1999, section 2.2): each component is a field like any other and is
// carried by the flow. All three components are traced back with the OLD velocity (u0, v0, w0), so the
// result does not depend on the order in which components or cells are processed.
// M4-015 (kernel fusion): the three components share one back-trace per cell (advectRow3), with the same
// result, bit for bit, as three separate advectScalar calls.
void advectVelocity(SmokeState& s, float dt, SlabRange range) {
    s.u.swap(s.u0);
    s.v.swap(s.v0);
    s.w.swap(s.w0);
    Field3D* const dst[3] = {&s.u, &s.v, &s.w};
    const Field3D* const src[3] = {&s.u0, &s.v0, &s.w0};
    advectFields(dst, src, 3, s.u0, s.v0, s.w0, dt, range);
    setVelocityBoundary(s);
}

// M4-016 (kernel fusion): density and temperature are carried by the same velocity, so they share one
// back-trace per cell (advectRow2); bit-identical to two separate advectScalar calls.
void advectScalars(SmokeState& s, float dt, SlabRange range) {
    // Double buffering: after the swap, density0 / temperature0 hold the old values and the main fields
    // are overwritten.
    s.density.swap(s.density0);
    s.temperature.swap(s.temperature0);
    Field3D* const dst[2] = {&s.density, &s.temperature};
    const Field3D* const src[2] = {&s.density0, &s.temperature0};
    advectFields(dst, src, 2, s.u, s.v, s.w, dt, range);
    setBoundary(s.density, BoundaryKind::Scalar);
    setBoundary(s.temperature, BoundaryKind::Scalar);
}

} // namespace smoke::serial
