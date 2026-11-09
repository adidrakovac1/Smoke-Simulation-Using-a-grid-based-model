// AVX2 build of the vorticity confinement kernels (M4-020, lab techniques T15/T16), like Projection_avx2.cpp
// and Advection_avx2.cpp. CMake compiles THIS FILE ONLY with -mavx2 (and -fno-math-errno, M4-018); callers
// must check smoke::avx2Supported() first (Forces.cpp does). -mavx2 does not enable FMA: bit-identical results.
//
// Only raw pointers and `static`/anonymous-namespace code are used here: see ForcesRows.inc.

#include "Restrict.h"

#include <cmath>
#include <cstddef>

namespace smoke::serial {

namespace {
#include "stages/ForcesRows.inc"
} // namespace

void vorticitySlabAvx2(float* u, float* v, float* w, float* ox, float* oy, float* oz, float* mag, int nx, int ny,
                       int k0, int k1, std::ptrdiff_t sJ, std::ptrdiff_t sK) {
    vorticitySlab(VorticityFields{u, v, w, ox, oy, oz, mag}, nx, ny, k0, k1, sJ, sK);
}

void confinementSlabAvx2(float* u, float* v, float* w, float* ox, float* oy, float* oz, float* mag, int nx, int ny,
                         int k0, int k1, float epsilonDt, std::ptrdiff_t sJ, std::ptrdiff_t sK) {
    confinementSlab(VorticityFields{u, v, w, ox, oy, oz, mag}, nx, ny, k0, k1, epsilonDt, sJ, sK);
}

} // namespace smoke::serial
