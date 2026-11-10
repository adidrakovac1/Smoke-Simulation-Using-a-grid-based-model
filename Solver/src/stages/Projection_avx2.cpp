// AVX2 build of the Jacobi kernel (M4-009, lab techniques T15/T16, docs/plan/OPTIMIZATION.md).
// CMake compiles THIS FILE ONLY with -mavx2 (/arch:AVX2 on MSVC); the rest of the program keeps the default
// flags and runs on any x86-64 CPU. Callers must check smoke::avx2Supported() first (Projection.cpp does).
// -mavx2 does not enable FMA, so the arithmetic is the same as the scalar path (bit-identical results).
//
// Keep this file free of shared inline code (no Field3D, no project headers except Restrict.h): see the
// note in JacobiRows.inc.

#include "Restrict.h"

#include <cstddef>
#include <immintrin.h>

namespace smoke::serial {

namespace {
#include "stages/JacobiRows.inc"
} // namespace

void jacobiSlabAvx2(float* pNew, const float* pOld, const float* div, int nx, int ny, int k0, int k1,
                    std::ptrdiff_t sJ, std::ptrdiff_t sK) {
    jacobiSlab(pNew, pOld, div, nx, ny, k0, k1, sJ, sK);  // the internal copy compiled with -mavx2
}

// T9 (M4-010): the same Jacobi row written by hand with AVX2 intrinsics, 8 floats per instruction, like
// saxpy_avx2 in the lab example. The six neighbours are added in the SAME ORDER as the scalar code and the
// division by 6 is kept (no FMA, no reciprocal), so the result is bit-identical. Loads are unaligned
// (loadu): i - 1 and i + 1 can never both be 32-byte aligned. The tail (nx % 8 cells) is done in scalar code.
void jacobiSlabAvx2Intrinsics(float* pNew, const float* pOld, const float* div, int nx, int ny, int k0, int k1,
                              std::ptrdiff_t sJ, std::ptrdiff_t sK) {
    const __m256 six = _mm256_set1_ps(6.0f);
    for (int k = k0; k < k1; ++k)
        for (int j = 1; j <= ny; ++j) {
            const std::ptrdiff_t row = j * sJ + k * sK;
            float* SMOKE_RESTRICT pn = pNew + row;
            const float* SMOKE_RESTRICT po = pOld + row;
            const float* SMOKE_RESTRICT d = div + row;
            int i = 1;
            for (; i + 7 <= nx; i += 8) {
                __m256 sum = _mm256_add_ps(_mm256_loadu_ps(po + i - 1), _mm256_loadu_ps(po + i + 1));
                sum = _mm256_add_ps(sum, _mm256_loadu_ps(po + i - sJ));
                sum = _mm256_add_ps(sum, _mm256_loadu_ps(po + i + sJ));
                sum = _mm256_add_ps(sum, _mm256_loadu_ps(po + i - sK));
                sum = _mm256_add_ps(sum, _mm256_loadu_ps(po + i + sK));
                _mm256_storeu_ps(pn + i, _mm256_div_ps(_mm256_sub_ps(sum, _mm256_loadu_ps(d + i)), six));
            }
            for (; i <= nx; ++i) {
                const float neighbours = po[i - 1] + po[i + 1] + po[i - sJ] + po[i + sJ] + po[i - sK] + po[i + sK];
                pn[i] = (neighbours - d[i]) / 6.0f;
            }
        }
}

} // namespace smoke::serial
