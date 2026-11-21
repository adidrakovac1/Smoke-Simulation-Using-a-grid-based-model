// D19: semi-Lagrangian advection rows written by hand with AVX2 intrinsics (lab technique T9), using the
// hardware gather instruction vgatherdps for the eight trilinear corners.
//
// Why. The auto-vectorized rows (AdvectionRows.inc, compiled with -mavx2) do not use vgatherdps: g++ builds
// every gathered vector from 8 scalar loads and 8 inserts ("emulated gather", objdump shows no vgatherdps).
// One advected field needs 8 such vectors per 8 cells, so the advection is limited by these loads and inserts.
//
// Bit-identical by construction: the same operations in the same order as clampToBox and sampleTrilinear
// (Interpolation.h), each one an IEEE single-precision operation, no FMA (-mavx2 does not enable it):
//   * maxOf(x, lo) = (x < lo) ? lo : x  is _mm256_max_ps(lo, x) (returns its SECOND operand unless lo > x;
//     for a NaN x that is x, as in maxOf); minOf(a, hi) = (hi < a) ? hi : a  is _mm256_min_ps(hi, a);
//   * static_cast<int>(x) truncates: _mm256_cvttps_epi32; static_cast<float>(int): _mm256_cvtepi32_ps;
//   * the cell index i0 + sj*j0 + sk*k0 is the same 32-bit integer (no overflow below 2^31 floats, asserted);
//   * the weights and the nested sums are evaluated exactly as written in sampleTrilinear.
// The tail of a row (nx % 8 cells) uses the scalar helpers themselves. A test compares every build bit for bit.
//
// CMake compiles this file only with -mavx2; callers check smoke::avx2Supported() first (Advection.cpp).

#include "stages/Interpolation.h"

#include "Restrict.h"

#include <cstddef>
#include <immintrin.h>

namespace smoke::serial {

namespace {

struct Corners {
    __m256i c;                         // index of corner (i0, j0, k0)
    __m256  sx0, sx1, sy0, sy1, sz0, sz1;
};

inline __m256 clampToBox8(__m256 x, __m256 lo, __m256 hi) { return _mm256_min_ps(hi, _mm256_max_ps(lo, x)); }

inline Corners corners8(__m256 x, __m256 y, __m256 z, __m256i sj, __m256i sk) {
    Corners t;
    const __m256i i0 = _mm256_cvttps_epi32(x);
    const __m256i j0 = _mm256_cvttps_epi32(y);
    const __m256i k0 = _mm256_cvttps_epi32(z);
    const __m256  one = _mm256_set1_ps(1.0f);
    t.sx1 = _mm256_sub_ps(x, _mm256_cvtepi32_ps(i0));
    t.sx0 = _mm256_sub_ps(one, t.sx1);
    t.sy1 = _mm256_sub_ps(y, _mm256_cvtepi32_ps(j0));
    t.sy0 = _mm256_sub_ps(one, t.sy1);
    t.sz1 = _mm256_sub_ps(z, _mm256_cvtepi32_ps(k0));
    t.sz0 = _mm256_sub_ps(one, t.sz1);
    t.c   = _mm256_add_epi32(_mm256_add_epi32(i0, _mm256_mullo_epi32(sj, j0)), _mm256_mullo_epi32(sk, k0));
    return t;
}

// sampleTrilinear for 8 points:
//   sz0 * (sy0 * (sx0 * f[c] + sx1 * f[c + 1]) + sy1 * (sx0 * f[c + sj] + sx1 * f[c + sj + 1])) +
//   sz1 * (sy0 * (sx0 * f[c + sk] + sx1 * f[c + sk + 1]) + sy1 * (sx0 * f[c + sk + sj] + sx1 * f[c + sk + sj + 1]))
inline __m256 sample8(const float* f, const Corners& t, __m256i sj, __m256i sk) {
    const __m256i one = _mm256_set1_epi32(1);
    const __m256i c   = t.c;
    const __m256i c1  = _mm256_add_epi32(c, one);
    const __m256i cj  = _mm256_add_epi32(c, sj);
    const __m256i cj1 = _mm256_add_epi32(cj, one);
    const __m256i ck  = _mm256_add_epi32(c, sk);
    const __m256i ck1 = _mm256_add_epi32(ck, one);
    const __m256i ckj = _mm256_add_epi32(ck, sj);   // c + sk + sj
    const __m256i ckj1 = _mm256_add_epi32(ckj, one);
    const __m256 f000 = _mm256_i32gather_ps(f, c, 4);
    const __m256 f100 = _mm256_i32gather_ps(f, c1, 4);
    const __m256 f010 = _mm256_i32gather_ps(f, cj, 4);
    const __m256 f110 = _mm256_i32gather_ps(f, cj1, 4);
    const __m256 f001 = _mm256_i32gather_ps(f, ck, 4);
    const __m256 f101 = _mm256_i32gather_ps(f, ck1, 4);
    const __m256 f011 = _mm256_i32gather_ps(f, ckj, 4);
    const __m256 f111 = _mm256_i32gather_ps(f, ckj1, 4);
    const __m256 a  = _mm256_add_ps(_mm256_mul_ps(t.sx0, f000), _mm256_mul_ps(t.sx1, f100));
    const __m256 b  = _mm256_add_ps(_mm256_mul_ps(t.sx0, f010), _mm256_mul_ps(t.sx1, f110));
    const __m256 lo = _mm256_add_ps(_mm256_mul_ps(t.sy0, a), _mm256_mul_ps(t.sy1, b));
    const __m256 d  = _mm256_add_ps(_mm256_mul_ps(t.sx0, f001), _mm256_mul_ps(t.sx1, f101));
    const __m256 e  = _mm256_add_ps(_mm256_mul_ps(t.sx0, f011), _mm256_mul_ps(t.sx1, f111));
    const __m256 hi = _mm256_add_ps(_mm256_mul_ps(t.sy0, d), _mm256_mul_ps(t.sy1, e));
    return _mm256_add_ps(_mm256_mul_ps(t.sz0, lo), _mm256_mul_ps(t.sz1, hi));
}

// One row: `count` fields src[n] -> dst[n] carried by u, v, w. With p != nullptr the velocity of each cell is
// first corrected by the pressure gradient and stored back (correctAdvectRow2 in AdvectionRows.inc).
template <int Count, bool Correct>
void row8(float* const* dst, const float* const* src, float* u, float* v, float* w, const float* p,
          const GridSize s, float fj, float fk, float dt, std::ptrdiff_t sJ, std::ptrdiff_t sK) {
    const __m256i sj = _mm256_set1_epi32(static_cast<int>(sJ));
    const __m256i sk = _mm256_set1_epi32(static_cast<int>(sK));
    const __m256  dtv = _mm256_set1_ps(dt);
    const __m256  half = _mm256_set1_ps(0.5f);
    const __m256  lo = _mm256_set1_ps(0.5f);
    const __m256  hiX = _mm256_set1_ps(static_cast<float>(s.nx) + 0.5f);
    const __m256  hiY = _mm256_set1_ps(static_cast<float>(s.ny) + 0.5f);
    const __m256  hiZ = _mm256_set1_ps(static_cast<float>(s.nz) + 0.5f);
    const __m256  fjv = _mm256_set1_ps(fj);
    const __m256  fkv = _mm256_set1_ps(fk);
    const __m256i lane = _mm256_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7);
    int i = 1;
    for (; i + 7 <= s.nx; i += 8) {
        __m256 uc = _mm256_loadu_ps(u + i);
        __m256 vc = _mm256_loadu_ps(v + i);
        __m256 wc = _mm256_loadu_ps(w + i);
        if constexpr (Correct) {
            // u - 0.5f * (p[i + 1] - p[i - 1]), and the same for v (in j) and w (in k)
            uc = _mm256_sub_ps(uc, _mm256_mul_ps(half, _mm256_sub_ps(_mm256_loadu_ps(p + i + 1), _mm256_loadu_ps(p + i - 1))));
            vc = _mm256_sub_ps(vc, _mm256_mul_ps(half, _mm256_sub_ps(_mm256_loadu_ps(p + i + sJ), _mm256_loadu_ps(p + i - sJ))));
            wc = _mm256_sub_ps(wc, _mm256_mul_ps(half, _mm256_sub_ps(_mm256_loadu_ps(p + i + sK), _mm256_loadu_ps(p + i - sK))));
            _mm256_storeu_ps(u + i, uc);
            _mm256_storeu_ps(v + i, vc);
            _mm256_storeu_ps(w + i, wc);
        }
        const __m256 fi = _mm256_cvtepi32_ps(_mm256_add_epi32(_mm256_set1_epi32(i), lane));
        const __m256 x = clampToBox8(_mm256_sub_ps(fi, _mm256_mul_ps(dtv, uc)), lo, hiX);
        const __m256 y = clampToBox8(_mm256_sub_ps(fjv, _mm256_mul_ps(dtv, vc)), lo, hiY);
        const __m256 z = clampToBox8(_mm256_sub_ps(fkv, _mm256_mul_ps(dtv, wc)), lo, hiZ);
        const Corners t = corners8(x, y, z, sj, sk);
        for (int n = 0; n < Count; ++n) _mm256_storeu_ps(dst[n] + i, sample8(src[n], t, sj, sk));
    }
    for (; i <= s.nx; ++i) {  // tail: the scalar code itself
        float uc = u[i], vc = v[i], wc = w[i];
        if constexpr (Correct) {
            uc = u[i] - 0.5f * (p[i + 1] - p[i - 1]);
            vc = v[i] - 0.5f * (p[i + sJ] - p[i - sJ]);
            wc = w[i] - 0.5f * (p[i + sK] - p[i - sK]);
            u[i] = uc;
            v[i] = vc;
            w[i] = wc;
        }
        const float x = clampToBox(static_cast<float>(i) - dt * uc, s.nx);
        const float y = clampToBox(fj - dt * vc, s.ny);
        const float z = clampToBox(fk - dt * wc, s.nz);
        for (int n = 0; n < Count; ++n) dst[n][i] = sampleTrilinear(src[n], sJ, sK, x, y, z);
    }
}

template <int Count, bool Correct>
void slab8(float* const* dst, const float* const* src, float* u, float* v, float* w, const float* p,
           const GridSize s, int k0, int k1, float dt, std::ptrdiff_t sJ, std::ptrdiff_t sK) {
    for (int k = k0; k < k1; ++k)
        for (int j = 1; j <= s.ny; ++j) {
            const std::ptrdiff_t row = j * sJ + k * sK;
            float* d[3] = {};
            for (int n = 0; n < Count; ++n) d[n] = dst[n] + row;
            row8<Count, Correct>(d, src, u + row, v + row, w + row, Correct ? p + row : nullptr, s,
                                 static_cast<float>(j), static_cast<float>(k), dt, sJ, sK);
        }
}

} // namespace

void advectSlabGatherAvx2(float* const* dst, const float* const* src, int count, const float* u, const float* v,
                          const float* w, const GridSize s, int k0, int k1, float dt, std::ptrdiff_t sJ,
                          std::ptrdiff_t sK) {
    // Without the correction u, v, w are only read; the template takes them as float* for the other case.
    float* uu = const_cast<float*>(u);
    float* vv = const_cast<float*>(v);
    float* ww = const_cast<float*>(w);
    if (count == 3)
        slab8<3, false>(dst, src, uu, vv, ww, nullptr, s, k0, k1, dt, sJ, sK);
    else if (count == 2)
        slab8<2, false>(dst, src, uu, vv, ww, nullptr, s, k0, k1, dt, sJ, sK);
    else
        slab8<1, false>(dst, src, uu, vv, ww, nullptr, s, k0, k1, dt, sJ, sK);
}

void correctAdvectSlab2GatherAvx2(float* d0, float* d1, float* u, float* v, float* w, const float* p,
                                  const float* s0, const float* s1, const GridSize s, int k0, int k1, float dt,
                                  std::ptrdiff_t sJ, std::ptrdiff_t sK) {
    float* const       dst[2] = {d0, d1};
    const float* const src[2] = {s0, s1};
    slab8<2, true>(dst, src, u, v, w, p, s, k0, k1, dt, sJ, sK);
}

} // namespace smoke::serial
