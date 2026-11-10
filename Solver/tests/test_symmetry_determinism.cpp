// T6 (symmetry) and T7 (determinism), docs/plan/VERIFICATION.md, section 1.
#include "doctest/doctest.h"
#include "smoke/SmokeSolver.h"
#include "stages/Stages.h"
#include "test_helpers.h"

#include <cstring>

using smoke::GridSize;
using smoke::SmokeParams;
using smoke::SmokeState;
using namespace smoke::serial;
using namespace smoke_test;

namespace {

// A mirror-symmetric scenario in x and z: a source centred in x and z, and a velocity field that is
// mirror-symmetric too. u = c * (i - cx) / nx is odd under i -> nx + 1 - i (exactly, in float, because
// i - cx is a multiple of 0.5), w likewise in z, and v is constant. The flow spreads the smoke sideways,
// so the interpolation weights are non-trivial and rounding differences between a cell and its mirror
// image can show up.
void runSymmetricScenario(SmokeState& s, int steps) {
    const GridSize g{32, 32, 32};
    s.resize(g);
    const float cx = 0.5f * float(g.nx + 1), cz = 0.5f * float(g.nz + 1);
    for (int k = 1; k <= g.nz; ++k)
        for (int j = 1; j <= g.ny; ++j)
            for (int i = 1; i <= g.nx; ++i) {
                s.u.at(i, j, k) = 40.0f * (float(i) - cx) / float(g.nx);
                s.v.at(i, j, k) = 10.0f;
                s.w.at(i, j, k) = 40.0f * (float(k) - cz) / float(g.nz);
            }
    setVelocityBoundary(s);

    SmokeParams p;
    p.source = {0.5f, 0.25f, 0.5f, 0.15f, 1.0f, 1.0f};
    const float dt = 1.0f / 60.0f;
    for (int step = 0; step < steps; ++step) {
        addSources(s, p, dt, wholeInterior(g));
        advectScalars(s, dt, wholeInterior(g));
    }
}

} // namespace

TEST_CASE("T6_symmetric_input_gives_symmetric_density") {
    SmokeState s;
    runSymmetricScenario(s, 30);
    const float peak = maxInterior(s.density);
    const float relX = mirrorErrorX(s.density) / peak;
    const float relZ = mirrorErrorZ(s.density) / peak;
    MESSAGE("T6 measured: max density " << peak << ", relative mirror error x = " << relX << ", z = " << relZ);
    CHECK(peak > 0.0f);
    CHECK(countNonFinite(s.density) == 0);
    // Not bit-exact: the mirrored back-trace (n+1-i) + dt*u rounds differently from i - dt*u
    // (docs/plan/VERIFICATION.md, note on T6). Measured 2026-11-09 (32^3, 30 steps, g++ 16.2.0 Release):
    // relative mirror error 1.8e-6 in x and in z, about 15 float ulps. Limit 1e-5 (~5x margin); an indexing
    // bug (e.g. an i/j mix-up or an off-by-one in the source) gives errors of order 1e-1.
    CHECK(relX < 1e-5f);
    CHECK(relZ < 1e-5f);
}

TEST_CASE("T7_same_input_gives_bit_identical_results") {
    SmokeState a, b;
    runSymmetricScenario(a, 30);
    runSymmetricScenario(b, 30);
    CHECK(cellsDifferent(a.density, b.density) == 0);
    CHECK(cellsDifferent(a.temperature, b.temperature) == 0);
}

TEST_CASE("T7_solver_steps_are_deterministic") {
    // Through the public interface: two solvers, same configuration, same number of steps.
    smoke::SolverConfig cfg;
    cfg.grid          = {24, 24, 24};
    cfg.params.source = {0.5f, 0.2f, 0.5f, 0.2f, 1.0f, 1.0f};
    smoke::SmokeSolver a(cfg), b(cfg);
    for (int i = 0; i < 20; ++i) {
        a.step();
        b.step();
    }
    const smoke::DensityView va = a.density(), vb = b.density();
    long long diff = 0;
    for (int k = 0; k < va.nz; ++k)
        for (int j = 0; j < va.ny; ++j)
            for (int i = 0; i < va.nx; ++i) {
                const long long idx = i + 1LL * va.strideJ * j + 1LL * va.strideK * k;
                diff += std::memcmp(&va.origin[idx], &vb.origin[idx], sizeof(float)) != 0;
            }
    CHECK(diff == 0);
    CHECK(a.stepCount() == 20);
}
