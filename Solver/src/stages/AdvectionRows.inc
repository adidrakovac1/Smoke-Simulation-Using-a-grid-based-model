// Semi-Lagrangian advection slab kernel on raw pointers, shared by Advection.cpp (default flags) and
// Advection_avx2.cpp (compiled with -mavx2, technique T16, M4-014). Include it ONLY inside an anonymous
// namespace, so each translation unit has its own internal copy compiled with its own flags. It calls only
// the `static inline` helpers of Interpolation.h (also one copy per translation unit), never Field3D.
// Requires Interpolation.h, Restrict.h, smoke/GridSize.h and <cstddef>. Row (j, k) starts at j*sJ + k*sK.

// T12 (M4-012): the innermost loop is a row function with SMOKE_RESTRICT pointer parameters, kept out of line
// with SMOKE_NOINLINE so that g++ can vectorize it (M4-013, see Restrict.h). `dst` is never
// the same array as the others. `src` may be the same array as u, v or w (advectVelocity advects u0 by
// u0, v0, w0); that is allowed because those are only read.
SMOKE_NOINLINE void advectRow(float* SMOKE_RESTRICT dst, const float* SMOKE_RESTRICT u, const float* SMOKE_RESTRICT v,
               const float* SMOKE_RESTRICT w, const float* SMOKE_RESTRICT src, const smoke::GridSize s, float fj,
               float fk, float dt, std::ptrdiff_t sJ, std::ptrdiff_t sK) {
    for (int i = 1; i <= s.nx; ++i) {
        const float x = clampToBox(static_cast<float>(i) - dt * u[i], s.nx);
        const float y = clampToBox(fj - dt * v[i], s.ny);
        const float z = clampToBox(fk - dt * w[i], s.nz);
        dst[i] = sampleTrilinear(src, sJ, sK, x, y, z);
    }
}

// Kernel fusion (M4-015, M4-016): fields carried by the SAME velocity share the back-trace. The trace, the
// clamp, the cell index and the 6 interpolation weights are computed once per cell and used for 2 or 3
// source fields, and u, v, w are read once instead of 2 or 3 times. Each output uses exactly the same
// x, y, z and the same sampleTrilinear as advectRow, so it is bit-identical to separate advectRow calls.
SMOKE_NOINLINE void advectRow2(float* SMOKE_RESTRICT d0, float* SMOKE_RESTRICT d1, const float* SMOKE_RESTRICT u,
                const float* SMOKE_RESTRICT v, const float* SMOKE_RESTRICT w, const float* SMOKE_RESTRICT s0,
                const float* SMOKE_RESTRICT s1, const smoke::GridSize s, float fj, float fk, float dt,
                std::ptrdiff_t sJ, std::ptrdiff_t sK) {
    for (int i = 1; i <= s.nx; ++i) {
        const float x = clampToBox(static_cast<float>(i) - dt * u[i], s.nx);
        const float y = clampToBox(fj - dt * v[i], s.ny);
        const float z = clampToBox(fk - dt * w[i], s.nz);
        d0[i] = sampleTrilinear(s0, sJ, sK, x, y, z);
        d1[i] = sampleTrilinear(s1, sJ, sK, x, y, z);
    }
}

SMOKE_NOINLINE void advectRow3(float* SMOKE_RESTRICT d0, float* SMOKE_RESTRICT d1, float* SMOKE_RESTRICT d2,
                const float* SMOKE_RESTRICT u, const float* SMOKE_RESTRICT v, const float* SMOKE_RESTRICT w,
                const float* SMOKE_RESTRICT s0, const float* SMOKE_RESTRICT s1, const float* SMOKE_RESTRICT s2,
                const smoke::GridSize s, float fj, float fk, float dt, std::ptrdiff_t sJ, std::ptrdiff_t sK) {
    for (int i = 1; i <= s.nx; ++i) {
        const float x = clampToBox(static_cast<float>(i) - dt * u[i], s.nx);
        const float y = clampToBox(fj - dt * v[i], s.ny);
        const float z = clampToBox(fk - dt * w[i], s.nz);
        d0[i] = sampleTrilinear(s0, sJ, sK, x, y, z);
        d1[i] = sampleTrilinear(s1, sJ, sK, x, y, z);
        d2[i] = sampleTrilinear(s2, sJ, sK, x, y, z);
    }
}

// `count` (1, 2 or 3) fields src[f] -> dst[f], all carried by (u, v, w).
void advectSlab(float* const* dst, const float* const* src, int count, const float* u, const float* v,
                const float* w, const smoke::GridSize s, int k0, int k1, float dt, std::ptrdiff_t sJ,
                std::ptrdiff_t sK) {
    for (int k = k0; k < k1; ++k)
        for (int j = 1; j <= s.ny; ++j) {
            const std::ptrdiff_t row = j * sJ + k * sK;
            const float fj = static_cast<float>(j), fk = static_cast<float>(k);
            if (count == 3)
                advectRow3(dst[0] + row, dst[1] + row, dst[2] + row, u + row, v + row, w + row, src[0], src[1],
                           src[2], s, fj, fk, dt, sJ, sK);
            else if (count == 2)
                advectRow2(dst[0] + row, dst[1] + row, u + row, v + row, w + row, src[0], src[1], s, fj, fk, dt,
                           sJ, sK);
            else
                advectRow(dst[0] + row, u + row, v + row, w + row, src[0], s, fj, fk, dt, sJ, sK);
        }
}
