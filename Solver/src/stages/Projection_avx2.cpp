// AVX2 build of the Jacobi kernel (M4-009, lab techniques T15/T16, docs/plan/OPTIMIZATION.md).
// CMake compiles THIS FILE ONLY with -mavx2 (/arch:AVX2 on MSVC); the rest of the program keeps the default
// flags and runs on any x86-64 CPU. Callers must check smoke::avx2Supported() first (Projection.cpp does).
// -mavx2 does not enable FMA, so the arithmetic is the same as the scalar path (bit-identical results).
//
// Keep this file free of shared inline code (no Field3D, no project headers except Restrict.h): see the
// note in JacobiRows.inc.

#include "Restrict.h"

#include <cstddef>

namespace smoke::serial {

namespace {
#include "stages/JacobiRows.inc"
} // namespace

void jacobiSlabAvx2(float* pNew, const float* pOld, const float* div, int nx, int ny, int k0, int k1,
                    std::ptrdiff_t sJ, std::ptrdiff_t sK) {
    jacobiSlab(pNew, pOld, div, nx, ny, k0, k1, sJ, sK);  // the internal copy compiled with -mavx2
}

} // namespace smoke::serial
