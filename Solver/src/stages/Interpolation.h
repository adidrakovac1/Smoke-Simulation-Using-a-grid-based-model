#pragma once

// Small helpers used per cell inside the advection loop. They are inline in a header so the compiler
// can inline them into the innermost loop (docs/plan/ARCHITECTURE.md, section 5, rule 4).

#include "smoke/Field3D.h"

#include <cstddef>

namespace smoke::serial {

/// Clamp a back-traced coordinate to the box. Interior cell i covers [i - 0.5, i + 0.5], so the box
/// spans [0.5, n + 0.5] along an axis with n interior cells. This is the clamp used by advect() in
/// Stam 2003, and it matches clipping the path at the wall in Fedkiw et al. 2001, fig. 2
/// (docs/plan/MATH.md, section 4).
///
/// Branchless (M4-011, lab technique T10): min/max compile to minss/maxss instead of jumps, so the loop has
/// no control flow and can be vectorized. Same result as "x < lo ? lo : (x > hi ? hi : x)" for every input,
/// NaN included (max(NaN, lo) and min(NaN, hi) both return the NaN).
///
/// Linkage (M4-014): the helpers in this header are `static inline`, so every translation unit gets its OWN
/// copy. Advection_avx2.cpp includes this header with -mavx2; with ordinary inline (or std::min/std::max,
/// which are templates) the linker could keep the AVX2 copy for the whole program, and the program would
/// then crash on a CPU without AVX2. minOf/maxOf are written exactly like std::min/std::max.
static inline float maxOf(float a, float b) { return (a < b) ? b : a; }  // == std::max(a, b)
static inline float minOf(float a, float b) { return (b < a) ? b : a; }  // == std::min(a, b)

static inline float clampToBox(float x, int n) {
    const float lo = 0.5f;
    const float hi = static_cast<float>(n) + 0.5f;
    return minOf(maxOf(x, lo), hi);
}

/// Trilinear interpolation of `f` at the point (x, y, z), given in cell-index coordinates
/// (cell (i,j,k) has its centre at (i,j,k)). The point must already be clamped with clampToBox, so all
/// eight neighbours exist (they may be ghost cells, which setBoundary keeps up to date).
///
/// The result is a weighted average of the 8 surrounding values with weights >= 0 that sum to 1, so it
/// always lies between their minimum and maximum (test T4, docs/plan/VERIFICATION.md).
///
/// Raw-pointer form (M4-012): `f` points at cell (0,0,0) of a field with strides sJ, sK. Same arithmetic in
/// the same order as the Field3D form below, which calls it, so both give bit-identical results.
///
/// 32-bit cell index (M4-013): the eight reads are f[c + offset] with an `int` index c. In this form g++ can
/// vectorize the advection loop (the reads become an emulated gather); with 64-bit pointer arithmetic it
/// reports "data ref analysis failed". The addresses are the same, so the result is the same. Requires
/// fewer than 2^31 floats per field (advectScalar asserts it; 128^3 is about 2.2 million).
static inline float sampleTrilinear(const float* f, std::ptrdiff_t sJ, std::ptrdiff_t sK, float x, float y, float z) {
    const int i0 = static_cast<int>(x);
    const int j0 = static_cast<int>(y);
    const int k0 = static_cast<int>(z);
    const float sx1 = x - static_cast<float>(i0), sx0 = 1.0f - sx1;
    const float sy1 = y - static_cast<float>(j0), sy0 = 1.0f - sy1;
    const float sz1 = z - static_cast<float>(k0), sz0 = 1.0f - sz1;
    const int sj = static_cast<int>(sJ), sk = static_cast<int>(sK);
    const int c = i0 + sj * j0 + sk * k0;  // cell (i0, j0, k0); the other seven are +1 / +sj / +sk
    return sz0 * (sy0 * (sx0 * f[c] + sx1 * f[c + 1]) +
                  sy1 * (sx0 * f[c + sj] + sx1 * f[c + sj + 1])) +
           sz1 * (sy0 * (sx0 * f[c + sk] + sx1 * f[c + sk + 1]) +
                  sy1 * (sx0 * f[c + sk + sj] + sx1 * f[c + sk + sj + 1]));
}

static inline float sampleTrilinear(const Field3D& f, float x, float y, float z) {
    return sampleTrilinear(f.data(), f.strideJ(), f.strideK(), x, y, z);
}

/// Original M1 form (Field3D::at per corner). Used by test_interpolation.cpp to check that the raw-pointer
/// form above gives bit-identical results; not used by the solver.
static inline float sampleTrilinearM1(const Field3D& f, float x, float y, float z) {
    // x >= 0.5 after clamping, so the int conversion (which truncates) is the same as floor().
    const int i0 = static_cast<int>(x);
    const int j0 = static_cast<int>(y);
    const int k0 = static_cast<int>(z);
    const int i1 = i0 + 1;
    const int j1 = j0 + 1;
    const int k1 = k0 + 1;

    const float sx1 = x - static_cast<float>(i0), sx0 = 1.0f - sx1;
    const float sy1 = y - static_cast<float>(j0), sy0 = 1.0f - sy1;
    const float sz1 = z - static_cast<float>(k0), sz0 = 1.0f - sz1;

    // Same nesting as Stam 2003 advect(), extended by one level for z.
    return sz0 * (sy0 * (sx0 * f.at(i0, j0, k0) + sx1 * f.at(i1, j0, k0)) +
                  sy1 * (sx0 * f.at(i0, j1, k0) + sx1 * f.at(i1, j1, k0))) +
           sz1 * (sy0 * (sx0 * f.at(i0, j0, k1) + sx1 * f.at(i1, j0, k1)) +
                  sy1 * (sx0 * f.at(i0, j1, k1) + sx1 * f.at(i1, j1, k1)));
}

} // namespace smoke::serial
