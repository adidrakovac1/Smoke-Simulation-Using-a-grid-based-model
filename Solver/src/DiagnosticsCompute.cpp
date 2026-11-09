#include "DiagnosticsCompute.h"

#include <algorithm>
#include <cmath>

namespace smoke {

namespace {

long long countNonFinite(const Field3D& f) {
    long long n = 0;
    const float* d = f.data();
    for (std::size_t idx = 0; idx < f.totalFloats(); ++idx) n += std::isfinite(d[idx]) ? 0 : 1;
    return n;
}

} // namespace

Diagnostics computeDiagnostics(const SmokeState& s, float dt) {
    Diagnostics d;
    const GridSize g = s.density.size();
    if (!g.valid() || s.density.data() == nullptr) return d;

    double maxDivBefore = 0.0, maxDivAfter = 0.0, mass = 0.0, maxSpeed2 = 0.0;
    for (int k = 1; k <= g.nz; ++k) {
        for (int j = 1; j <= g.ny; ++j) {
            for (int i = 1; i <= g.nx; ++i) {
                maxDivBefore = std::max(maxDivBefore, double(std::fabs(s.divergence.at(i, j, k))));

                // Same formula as computeDivergence (stages/Projection.cpp).
                const float divNow = 0.5f * (s.u.at(i + 1, j, k) - s.u.at(i - 1, j, k) +
                                             s.v.at(i, j + 1, k) - s.v.at(i, j - 1, k) +
                                             s.w.at(i, j, k + 1) - s.w.at(i, j, k - 1));
                maxDivAfter = std::max(maxDivAfter, double(std::fabs(divNow)));

                mass += s.density.at(i, j, k);

                const double u = s.u.at(i, j, k), v = s.v.at(i, j, k), w = s.w.at(i, j, k);
                maxSpeed2 = std::max(maxSpeed2, u * u + v * v + w * w);
            }
        }
    }

    d.computed       = true;
    d.maxDivBefore   = maxDivBefore;
    d.maxDivAfter    = maxDivAfter;
    d.totalMass      = mass;
    d.maxSpeed       = std::sqrt(maxSpeed2);
    d.cfl            = d.maxSpeed * double(dt);
    d.nonFiniteCells = countNonFinite(s.density) + countNonFinite(s.temperature) + countNonFinite(s.u) +
                       countNonFinite(s.v) + countNonFinite(s.w) + countNonFinite(s.pressure);
    return d;
}

} // namespace smoke
