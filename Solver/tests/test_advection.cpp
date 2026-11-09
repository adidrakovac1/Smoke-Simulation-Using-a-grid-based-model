// Semi-Lagrangian advection of scalars: T1, T2, T4, T5 (docs/plan/VERIFICATION.md, section 1).
#include "doctest/doctest.h"
#include "stages/Interpolation.h"
#include "stages/Stages.h"
#include "test_helpers.h"

#include <cfloat>
#include <cmath>

using smoke::Field3D;
using smoke::GridSize;
using smoke::SmokeState;
using namespace smoke::serial;
using namespace smoke_test;

namespace {

// Scenario for T1 and the M1 demo (docs/plan/IMPLEMENTATION_PLAN.md, section 4.5). These are choices of
// the test, not physical constants: a 64^3 grid, a sphere of radius 8 cells centred at (20, 32.5, 32.5),
// u = (30, 0, 0) cells/s and dt = 1/60 s, i.e. half a cell per step. After 40 steps the sphere is at
// x = 40 and it never comes closer than 11 cells to a wall.
struct SphereScenario {
    GridSize g{64, 64, 64};
    float    cx = 20.0f, cy = 32.5f, cz = 32.5f, radius = 8.0f;
    float    c = 30.0f;
    float    dt = 1.0f / 60.0f;
};

void setupSphere(SmokeState& s, const SphereScenario& sc) {
    s.resize(sc.g);
    fillSphere(s.density, sc.cx, sc.cy, sc.cz, sc.radius, 1.0f);
    setBoundary(s.density, BoundaryKind::Scalar);
    fillInterior(s.u, sc.c);
    setVelocityBoundary(s);
}

} // namespace

TEST_CASE("T2_zero_velocity_leaves_density_bit_for_bit_unchanged") {
    const GridSize g{12, 10, 8};
    SmokeState s;
    s.resize(g);
    SplitMix64 rng(5);
    fillRandom(s.density, rng, 0.0f, 1.0f);
    setBoundary(s.density, BoundaryKind::Scalar);
    const Field3D before = copyOf(s.density);

    // u = v = w = 0: the back-traced point is the cell centre itself, the weights are exactly 1 and 0.
    for (int step = 0; step < 10; ++step) advectScalars(s, 0.1f, wholeInterior(g));

    CHECK(cellsDifferent(s.density, before) == 0);
}

TEST_CASE("T4_advected_value_lies_between_the_8_source_values") {
    const GridSize g{16, 12, 10};
    SmokeState s;
    s.resize(g);
    SplitMix64 rng(6);
    fillRandom(s.density, rng, 0.0f, 1.0f);
    setBoundary(s.density, BoundaryKind::Scalar);
    // Fast random velocities (up to ~3 cells per step), so many points are also clamped at the walls.
    fillRandom(s.u, rng, -200.0f, 200.0f);
    fillRandom(s.v, rng, -200.0f, 200.0f);
    fillRandom(s.w, rng, -200.0f, 200.0f);
    setVelocityBoundary(s);
    const float dt = 1.0f / 60.0f;

    advectScalars(s, dt, wholeInterior(g));  // after this, density0 holds the old values

    long long outside = 0;
    for (int k = 1; k <= g.nz; ++k)
        for (int j = 1; j <= g.ny; ++j)
            for (int i = 1; i <= g.nx; ++i) {
                // Same back-trace as advectScalar, so we find the same 8 source cells.
                const float x = clampToBox(float(i) - dt * s.u.at(i, j, k), g.nx);
                const float y = clampToBox(float(j) - dt * s.v.at(i, j, k), g.ny);
                const float z = clampToBox(float(k) - dt * s.w.at(i, j, k), g.nz);
                const int i0 = int(x), j0 = int(y), k0 = int(z);
                float lo = s.density0.at(i0, j0, k0), hi = lo;
                for (int c = 0; c < 8; ++c) {
                    const float val = s.density0.at(i0 + (c & 1), j0 + ((c >> 1) & 1), k0 + ((c >> 2) & 1));
                    lo = std::fmin(lo, val);
                    hi = std::fmax(hi, val);
                }
                // The weights sum to 1 only up to rounding; three nested float blends can overshoot by a
                // few units in the last place, hence the 4 * FLT_EPSILON relative margin.
                const float margin = 4.0f * FLT_EPSILON * std::fmax(std::fabs(lo), std::fabs(hi));
                const float val = s.density.at(i, j, k);
                outside += (val < lo - margin || val > hi + margin);
            }
    CHECK(outside == 0);
}

TEST_CASE("T5_advection_keeps_the_ghost_layer_up_to_date") {
    const GridSize g{10, 9, 8};
    SmokeState s;
    s.resize(g);
    SplitMix64 rng(7);
    fillRandom(s.density, rng, 0.0f, 1.0f);
    fillRandom(s.temperature, rng, 0.0f, 1.0f);
    fillRandom(s.u, rng, -50.0f, 50.0f);
    fillRandom(s.v, rng, -50.0f, 50.0f);
    fillRandom(s.w, rng, -50.0f, 50.0f);
    advectScalars(s, 1.0f / 60.0f, wholeInterior(g));
    CHECK(ghostLayerErrors(s.density, false, false, false) == 0);
    CHECK(ghostLayerErrors(s.temperature, false, false, false) == 0);
}

TEST_CASE("T1_sphere_in_constant_velocity_moves_by_c_dt_per_step") {
    const SphereScenario sc;
    SmokeState s;
    setupSphere(s, sc);
    const double mass0 = totalMass(s.density);
    const double x0    = centroidX(s.density);
    const double shiftPerStep = double(sc.dt * sc.c);  // the same float product advectScalar uses
    const GridSize g = sc.g;

    advectScalars(s, sc.dt, wholeInterior(g));
    const double err1 = std::fabs((centroidX(s.density) - x0) - shiftPerStep);

    for (int step = 1; step < 40; ++step) advectScalars(s, sc.dt, wholeInterior(g));
    const double err40   = std::fabs((centroidX(s.density) - x0) - 40.0 * shiftPerStep);
    const double massRel = std::fabs(totalMass(s.density) - mass0) / mass0;

    MESSAGE("T1 measured: |shift error| after 1 step = " << err1 << " cells, after 40 steps = " << err40
            << " cells; relative mass change = " << massRel);
    CHECK(countNonFinite(s.density) == 0);

    // Tolerances from measurement (2026-11-09, 64^3, g++ 16.2.0 Release): error after 1 step 0, after 40
    // steps 4.9e-10 cells, relative mass change 2.4e-8. Theory says the shift is exact up to rounding
    // (docs/plan/IMPLEMENTATION_PLAN.md, section 4.4). The limits leave a large margin for rounding
    // (~1000x) and are still far below any real bug: a wrong sign or an off-by-one is >= 0.5 cells.
    CHECK(err1 < 1e-6);
    CHECK(err40 < 1e-6);
    CHECK(massRel < 1e-6);
}
