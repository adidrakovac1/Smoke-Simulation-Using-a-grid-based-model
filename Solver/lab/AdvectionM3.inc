// The advection kernel exactly as it was in M3 (commit 73ceb9e: Solver/src/stages/Advection.cpp and
// Interpolation.h, clamp with branches, Field3D::at per corner), kept here as history so smoke_lab can show
// where M4 started. Compiled twice, like JacobiM3.inc:
//   advection_m3_scalar.cpp  with vectorization switched off  -> version 1 (like B0)
//   advection_m3_auto.cpp    with the default Release flags   -> version 2 (like B1)
// Include it inside an anonymous namespace; it needs smoke/Field3D.h.

inline float clampToBoxM3(float x, int n) {
    const float lo = 0.5f;
    const float hi = static_cast<float>(n) + 0.5f;
    return x < lo ? lo : (x > hi ? hi : x);
}

inline float sampleTrilinearM3(const smoke::Field3D& f, float x, float y, float z) {
    const int i0 = static_cast<int>(x);
    const int j0 = static_cast<int>(y);
    const int k0 = static_cast<int>(z);
    const int i1 = i0 + 1;
    const int j1 = j0 + 1;
    const int k1 = k0 + 1;
    const float sx1 = x - static_cast<float>(i0), sx0 = 1.0f - sx1;
    const float sy1 = y - static_cast<float>(j0), sy0 = 1.0f - sy1;
    const float sz1 = z - static_cast<float>(k0), sz0 = 1.0f - sz1;
    return sz0 * (sy0 * (sx0 * f.at(i0, j0, k0) + sx1 * f.at(i1, j0, k0)) +
                  sy1 * (sx0 * f.at(i0, j1, k0) + sx1 * f.at(i1, j1, k0))) +
           sz1 * (sy0 * (sx0 * f.at(i0, j0, k1) + sx1 * f.at(i1, j0, k1)) +
                  sy1 * (sx0 * f.at(i0, j1, k1) + sx1 * f.at(i1, j1, k1)));
}

[[maybe_unused]] void advectM3(smoke::Field3D& dst, const smoke::Field3D& src, const smoke::Field3D& u, const smoke::Field3D& v,
              const smoke::Field3D& w, float dt) {
    const smoke::GridSize s = dst.size();
    for (int k = 1; k <= s.nz; ++k) {
        for (int j = 1; j <= s.ny; ++j) {
            for (int i = 1; i <= s.nx; ++i) {
                const float x = clampToBoxM3(static_cast<float>(i) - dt * u.at(i, j, k), s.nx);
                const float y = clampToBoxM3(static_cast<float>(j) - dt * v.at(i, j, k), s.ny);
                const float z = clampToBoxM3(static_cast<float>(k) - dt * w.at(i, j, k), s.nz);
                dst.at(i, j, k) = sampleTrilinearM3(src, x, y, z);
            }
        }
    }
}
