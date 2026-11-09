// Vorticity confinement row kernels on raw pointers (M4-019), shared by Forces.cpp (default flags) and
// Forces_avx2.cpp (compiled with -mavx2, M4-020). Include it ONLY inside an anonymous namespace, so each
// translation unit has its own internal copy compiled with its own flags. Requires Restrict.h, <cmath> and
// <cstddef>. Every pointer points at the first cell of a row (i = 0); neighbours in j and k are +-sJ, +-sK.
//
// T12: the row functions have SMOKE_RESTRICT parameters, so g++ knows that the outputs never overlap the
// inputs and can vectorize the i loop (with Field3D::at it reported "evolution of base is not affine").
// The expressions are exactly those of M3/M4-006 in the same order, so the result is bit-identical.
// std::sqrt is vectorized only with -fno-math-errno (CMake sets it for these files, M4-018): sqrt of a
// non-negative number is exact in IEEE 754, the flag only drops the errno update for negative input.

// Pass 1: vorticity w = curl u (central differences, h = 1) and its magnitude |w|.
SMOKE_NOINLINE void vorticityRow(float* SMOKE_RESTRICT ox, float* SMOKE_RESTRICT oy, float* SMOKE_RESTRICT oz,
                                 float* SMOKE_RESTRICT mag, const float* SMOKE_RESTRICT u,
                                 const float* SMOKE_RESTRICT v, const float* SMOKE_RESTRICT w, int n,
                                 std::ptrdiff_t sJ, std::ptrdiff_t sK) {
    for (int i = 1; i <= n; ++i) {
        const float wx = 0.5f * ((w[i + sJ] - w[i - sJ]) - (v[i + sK] - v[i - sK]));
        const float wy = 0.5f * ((u[i + sK] - u[i - sK]) - (w[i + 1] - w[i - 1]));
        const float wz = 0.5f * ((v[i + 1] - v[i - 1]) - (u[i + sJ] - u[i - sJ]));
        ox[i] = wx;
        oy[i] = wy;
        oz[i] = wz;
        mag[i] = std::sqrt(wx * wx + wy * wy + wz * wz);
    }
}

// Pass 2: N = grad|w| / |grad|w||, then u += eps * dt * (N x w). Branchless as in M4-003/M4-006.
SMOKE_NOINLINE void confinementRow(float* SMOKE_RESTRICT u, float* SMOKE_RESTRICT v, float* SMOKE_RESTRICT w,
                                   const float* SMOKE_RESTRICT mag, const float* SMOKE_RESTRICT ox,
                                   const float* SMOKE_RESTRICT oy, const float* SMOKE_RESTRICT oz, int n,
                                   float epsilonDt, std::ptrdiff_t sJ, std::ptrdiff_t sK) {
    for (int i = 1; i <= n; ++i) {
        const float nx = 0.5f * (mag[i + 1] - mag[i - 1]);
        const float ny = 0.5f * (mag[i + sJ] - mag[i - sJ]);
        const float nz = 0.5f * (mag[i + sK] - mag[i - sK]);
        const float gradientMagnitude = std::sqrt(nx * nx + ny * ny + nz * nz);
        // Same denominator as M4-003/M4-006 ("|grad| > 0 ? |grad| : 1"), written as an arithmetic mask: with a
        // ?: that chooses a divisor (or with static_cast<float>(p) and 1 - p), g++ 16 keeps a jump and does not
        // vectorize; this exact form is if-converted. p*|grad| + (!p)*1 is exactly |grad| (|grad| + 0) or
        // exactly 1 (0 + 1) for every |grad| >= 0. (For a NaN gradient the force is NaN either way, because
        // nx, ny, nz are NaN.)
        const bool p = gradientMagnitude > 0.0f;
        const float scale = epsilonDt / (p * gradientMagnitude + (!p) * 1.0f);  // h = 1
        const float wx = ox[i];
        const float wy = oy[i];
        const float wz = oz[i];
        u[i] += scale * (ny * wz - nz * wy);
        v[i] += scale * (nz * wx - nx * wz);
        w[i] += scale * (nx * wy - ny * wx);
    }
}

// Pointers of the fields the two passes use (all with the same strides).
struct VorticityFields {
    float* u;
    float* v;
    float* w;
    float* ox;
    float* oy;
    float* oz;
    float* mag;
};

void vorticitySlab(const VorticityFields& f, int nx, int ny, int k0, int k1, std::ptrdiff_t sJ, std::ptrdiff_t sK) {
    for (int k = k0; k < k1; ++k)
        for (int j = 1; j <= ny; ++j) {
            const std::ptrdiff_t r = j * sJ + k * sK;
            vorticityRow(f.ox + r, f.oy + r, f.oz + r, f.mag + r, f.u + r, f.v + r, f.w + r, nx, sJ, sK);
        }
}

void confinementSlab(const VorticityFields& f, int nx, int ny, int k0, int k1, float epsilonDt, std::ptrdiff_t sJ,
                     std::ptrdiff_t sK) {
    for (int k = k0; k < k1; ++k)
        for (int j = 1; j <= ny; ++j) {
            const std::ptrdiff_t r = j * sJ + k * sK;
            confinementRow(f.u + r, f.v + r, f.w + r, f.mag + r, f.ox + r, f.oy + r, f.oz + r, nx, epsilonDt, sJ,
                           sK);
        }
}
