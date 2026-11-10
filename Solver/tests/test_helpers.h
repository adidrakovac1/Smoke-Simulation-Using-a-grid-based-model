#pragma once

// Helpers shared by the tests (and by the advection demo). Test-only code.

#include "SmokeState.h"
#include "smoke/Field3D.h"

#include <cmath>
#include <cstdint>
#include <cstring>

namespace smoke_test {

/// Tiny deterministic random generator (splitmix64). We do not use <random> distributions because
/// std::uniform_real_distribution may give different numbers on different compilers, and the tests
/// must give the same inputs everywhere (docs/plan/IMPLEMENTATION_PLAN.md, section 3, rule 5).
struct SplitMix64 {
    std::uint64_t state;
    explicit SplitMix64(std::uint64_t seed) : state(seed) {}

    std::uint64_t next() {
        std::uint64_t z = (state += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    /// Uniform float in [lo, hi), built from the top 24 bits (exactly representable in float).
    float uniform(float lo, float hi) {
        const float unit = static_cast<float>(next() >> 40) * (1.0f / 16777216.0f);
        return lo + (hi - lo) * unit;
    }
};

/// Fill every cell, ghost layer included, with random values in [lo, hi).
inline void fillRandom(smoke::Field3D& f, SplitMix64& rng, float lo, float hi) {
    const smoke::GridSize s = f.size();
    for (int k = 0; k <= s.nz + 1; ++k)
        for (int j = 0; j <= s.ny + 1; ++j)
            for (int i = 0; i <= s.nx + 1; ++i) f.at(i, j, k) = rng.uniform(lo, hi);
}

/// Set interior cells whose centre is within `radius` of (cx, cy, cz) to `value` (cell-index coordinates).
inline void fillSphere(smoke::Field3D& f, float cx, float cy, float cz, float radius, float value) {
    const smoke::GridSize s = f.size();
    for (int k = 1; k <= s.nz; ++k)
        for (int j = 1; j <= s.ny; ++j)
            for (int i = 1; i <= s.nx; ++i) {
                const float dx = static_cast<float>(i) - cx;
                const float dy = static_cast<float>(j) - cy;
                const float dz = static_cast<float>(k) - cz;
                if (dx * dx + dy * dy + dz * dz <= radius * radius) f.at(i, j, k) = value;
            }
}

/// Set every interior cell to `value`.
inline void fillInterior(smoke::Field3D& f, float value) {
    const smoke::GridSize s = f.size();
    for (int k = 1; k <= s.nz; ++k)
        for (int j = 1; j <= s.ny; ++j)
            for (int i = 1; i <= s.nx; ++i) f.at(i, j, k) = value;
}

/// Sum over interior cells (in double, so the sum itself adds no visible error).
inline double totalMass(const smoke::Field3D& f) {
    const smoke::GridSize s = f.size();
    double m = 0.0;
    for (int k = 1; k <= s.nz; ++k)
        for (int j = 1; j <= s.ny; ++j)
            for (int i = 1; i <= s.nx; ++i) m += f.at(i, j, k);
    return m;
}

/// x coordinate of the centre of mass over interior cells (cell-index coordinates).
inline double centroidX(const smoke::Field3D& f) {
    const smoke::GridSize s = f.size();
    double m = 0.0, mx = 0.0;
    for (int k = 1; k <= s.nz; ++k)
        for (int j = 1; j <= s.ny; ++j)
            for (int i = 1; i <= s.nx; ++i) {
                m += f.at(i, j, k);
                mx += static_cast<double>(i) * f.at(i, j, k);
            }
    return m > 0.0 ? mx / m : 0.0;
}

/// Largest value over interior cells.
inline float maxInterior(const smoke::Field3D& f) {
    const smoke::GridSize s = f.size();
    float m = f.at(1, 1, 1);
    for (int k = 1; k <= s.nz; ++k)
        for (int j = 1; j <= s.ny; ++j)
            for (int i = 1; i <= s.nx; ++i) m = f.at(i, j, k) > m ? f.at(i, j, k) : m;
    return m;
}

/// Number of NaN / Inf values in the whole field (ghost layer included).
inline long long countNonFinite(const smoke::Field3D& f) {
    long long n = 0;
    for (std::size_t idx = 0; idx < f.totalFloats(); ++idx) n += std::isfinite(f.data()[idx]) ? 0 : 1;
    return n;
}

/// Largest |f(i,j,k) - f(mirror)| over interior cells, mirrored in x:  i -> nx + 1 - i.
inline float mirrorErrorX(const smoke::Field3D& f) {
    const smoke::GridSize s = f.size();
    float e = 0.0f;
    for (int k = 1; k <= s.nz; ++k)
        for (int j = 1; j <= s.ny; ++j)
            for (int i = 1; i <= s.nx; ++i) e = std::fmax(e, std::fabs(f.at(i, j, k) - f.at(s.nx + 1 - i, j, k)));
    return e;
}

/// Largest |f(i,j,k) - f(mirror)| over interior cells, mirrored in z:  k -> nz + 1 - k.
inline float mirrorErrorZ(const smoke::Field3D& f) {
    const smoke::GridSize s = f.size();
    float e = 0.0f;
    for (int k = 1; k <= s.nz; ++k)
        for (int j = 1; j <= s.ny; ++j)
            for (int i = 1; i <= s.nx; ++i) e = std::fmax(e, std::fabs(f.at(i, j, k) - f.at(i, j, s.nz + 1 - k)));
    return e;
}

} // namespace smoke_test

namespace smoke_test {

/// Number of cells (ghost layer included, padding excluded) that break the ghost-layer rule of
/// setBoundary (docs/plan/MATH.md, section 6): every ghost cell equals the interior cell reached by
/// clamping each index into [1, n], times -1 for every axis on which the index was a ghost index AND the
/// field is the velocity component normal to that axis. Interior cells always pass.
inline long long ghostLayerErrors(const smoke::Field3D& f, bool flipX, bool flipY, bool flipZ) {
    const smoke::GridSize s = f.size();
    auto clampIndex = [](int a, int n) { return a < 1 ? 1 : (a > n ? n : a); };
    long long bad = 0;
    for (int k = 0; k <= s.nz + 1; ++k)
        for (int j = 0; j <= s.ny + 1; ++j)
            for (int i = 0; i <= s.nx + 1; ++i) {
                const int ci = clampIndex(i, s.nx), cj = clampIndex(j, s.ny), ck = clampIndex(k, s.nz);
                float sign = 1.0f;
                if (flipX && ci != i) sign = -sign;
                if (flipY && cj != j) sign = -sign;
                if (flipZ && ck != k) sign = -sign;
                if (f.at(i, j, k) != sign * f.at(ci, cj, ck)) ++bad;
            }
    return bad;
}

/// Number of cells (ghost layer included, padding excluded) where a and b differ bit for bit.
inline long long cellsDifferent(const smoke::Field3D& a, const smoke::Field3D& b) {
    const smoke::GridSize s = a.size();
    long long n = 0;
    for (int k = 0; k <= s.nz + 1; ++k)
        for (int j = 0; j <= s.ny + 1; ++j)
            for (int i = 0; i <= s.nx + 1; ++i) {
                const float x = a.at(i, j, k), y = b.at(i, j, k);
                n += (std::memcmp(&x, &y, sizeof(float)) != 0) ? 1 : 0;
            }
    return n;
}

/// Deep copy of a field (same size, same values), for "before / after" comparisons in tests.
inline smoke::Field3D copyOf(const smoke::Field3D& f) {
    smoke::Field3D c(f.size());
    std::memcpy(c.data(), f.data(), f.totalFloats() * sizeof(float));
    return c;
}

} // namespace smoke_test
