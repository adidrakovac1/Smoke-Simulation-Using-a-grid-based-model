// AVX2 build of the advection kernel (M4-014, lab techniques T15/T16; T14 explains why the default build
// cannot vectorize it: the eight trilinear reads are at velocity-dependent addresses, a "gather", and SSE has
// no gather instruction, while AVX2 has vgatherdps). CMake compiles THIS FILE ONLY with -mavx2; callers must
// check smoke::avx2Supported() first (Advection.cpp does). -mavx2 does not enable FMA: bit-identical results.
//
// Only `static inline` helpers (Interpolation.h) and raw pointers are used here: see AdvectionRows.inc.

#include "stages/Interpolation.h"

#include "Restrict.h"

#include <cstddef>

namespace smoke::serial {

namespace {
#include "stages/AdvectionRows.inc"
} // namespace

void advectSlabAvx2(float* dst, const float* src, const float* u, const float* v, const float* w,
                    const GridSize s, int k0, int k1, float dt, std::ptrdiff_t sJ, std::ptrdiff_t sK) {
    advectSlab(dst, src, u, v, w, s, k0, k1, dt, sJ, sK);  // the internal copy compiled with -mavx2
}

} // namespace smoke::serial
