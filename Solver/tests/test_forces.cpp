// Buoyancy (Fedkiw et al. 2001, eq. 8) and velocity self-advection (Stam 1999, section 2.2).
#include "doctest/doctest.h"
#include "stages/Stages.h"
#include "test_helpers.h"

using smoke::GridSize;
using smoke::SmokeParams;
using smoke::SmokeState;
using namespace smoke::serial;
using namespace smoke_test;

TEST_CASE("buoyancy_hot_cell_is_pushed_up_by_dt_beta_dT") {
    const GridSize g{8, 8, 8};
    SmokeState s;
    s.resize(g);
    s.temperature.at(4, 5, 6) = 2.0f;  // 2 above ambient
    SmokeParams p;
    p.buoyancyAlpha = 0.0f;
    p.buoyancyBeta  = 1.5f;
    p.ambientTemp   = 0.0f;

    applyBuoyancy(s, p, 0.5f, wholeInterior(g));

    CHECK(s.v.at(4, 5, 6) == 1.5f);           // 0.5 * 1.5 * 2, exact in float
    CHECK(totalMass(s.v) == 1.5);             // no other cell changed
    CHECK(totalMass(s.u) == 0.0);             // buoyancy acts only along +y
    CHECK(totalMass(s.w) == 0.0);
    CHECK(ghostLayerErrors(s.v, false, true, false) == 0);
}

TEST_CASE("buoyancy_dense_smoke_is_pulled_down_and_ambient_temperature_gives_no_force") {
    const GridSize g{8, 8, 8};
    SmokeState s;
    s.resize(g);
    fillInterior(s.temperature, 3.0f);  // everywhere at ambient: no thermal force
    s.density.at(2, 2, 2) = 4.0f;
    SmokeParams p;
    p.buoyancyAlpha = 0.25f;
    p.buoyancyBeta  = 7.0f;
    p.ambientTemp   = 3.0f;

    applyBuoyancy(s, p, 0.5f, wholeInterior(g));

    CHECK(s.v.at(2, 2, 2) == -0.5f);  // 0.5 * (-0.25 * 4)
    CHECK(totalMass(s.v) == -0.5);
}

TEST_CASE("velocity_advection_keeps_a_constant_flow_constant_away_from_the_walls") {
    // A uniform flow carried by itself stays uniform. Near the x walls the ghost cells hold -c (no flow
    // through the wall), so we only look at cells whose back-traced point stays among interior cells.
    const GridSize g{16, 8, 8};
    SmokeState s;
    s.resize(g);
    const float c = 30.0f, dt = 1.0f / 60.0f;  // half a cell per step
    fillInterior(s.u, c);
    setVelocityBoundary(s);

    advectVelocity(s, dt, wholeInterior(g));

    long long wrong = 0;
    for (int k = 1; k <= g.nz; ++k)
        for (int j = 1; j <= g.ny; ++j)
            for (int i = 3; i <= g.nx; ++i) {
                wrong += !(s.u.at(i, j, k) == doctest::Approx(c));
                wrong += (s.v.at(i, j, k) != 0.0f) + (s.w.at(i, j, k) != 0.0f);
            }
    CHECK(wrong == 0);
    CHECK(ghostLayerErrors(s.u, true, false, false) == 0);
}

// D22: fans, u += dt * strength * (1 - a / reach) * n inside a cylinder (SmokeParams.h, SmokeFan).
TEST_CASE("D22 fan pushes only inside its cylinder, along its axis, weaker further away") {
    const GridSize g{16, 16, 16};
    SmokeState s;
    s.resize(g);
    SmokeParams p;
    p.vorticityEps = 0.0f;  // only the fan (applyForces also runs vorticity confinement)
    p.fanCount = 1;
    // Centre (4.5, 8.5, 8.5) in cells, blowing along +x, radius 2 cells, reach 8 cells.
    p.fans[0] = {0.25f, 0.5f, 0.5f, 1.0f, 0.0f, 0.0f, 0.125f, 0.5f, 4.0f};
    const float dt = 0.5f;

    applyForces(s, p, dt, wholeInterior(g));  // buoyancy is zero here (no density, no temperature)

    long long inside = 0, wrong = 0;
    for (int k = 1; k <= g.nz; ++k)
        for (int j = 1; j <= g.ny; ++j)
            for (int i = 1; i <= g.nx; ++i) {
                const float a  = i - 4.5f;
                const float r2 = (j - 8.5f) * (j - 8.5f) + (k - 8.5f) * (k - 8.5f);
                const bool  in = a >= 0.0f && a <= 8.0f && r2 <= 4.0f;
                const float expected = in ? dt * 4.0f * (1.0f - a / 8.0f) : 0.0f;
                inside += in;
                wrong += s.u.at(i, j, k) != doctest::Approx(expected);
                wrong += s.v.at(i, j, k) != 0.0f;  // only along x
                wrong += s.w.at(i, j, k) != 0.0f;
            }
    CHECK(inside > 0);
    CHECK(wrong == 0);
    CHECK(s.u.at(5, 9, 9) > s.u.at(10, 9, 9));  // fall-off along the axis
    CHECK(s.u.at(3, 9, 9) == 0.0f);             // nothing behind the fan
    CHECK(ghostLayerErrors(s.u, true, false, false) == 0);
}

TEST_CASE("D22 diagonal fan pushes along its normalized direction and zero fans change nothing") {
    const GridSize g{12, 12, 12};
    SmokeState s;
    s.resize(g);
    SmokeParams p;
    p.vorticityEps = 0.0f;  // only the fan (applyForces also runs vorticity confinement)
    p.fanCount = 1;
    p.fans[0] = {0.3f, 0.3f, 0.5f, 2.0f, 2.0f, 0.0f, 0.2f, 0.5f, 1.0f};  // (2, 2, 0) -> (0.707, 0.707, 0)
    applyForces(s, p, 1.0f, wholeInterior(g));
    CHECK(totalMass(s.u) > 0.0);
    CHECK(totalMass(s.u) == doctest::Approx(totalMass(s.v)));
    CHECK(totalMass(s.w) == 0.0);

    SmokeState t;
    t.resize(g);
    SmokeParams none = p;
    none.fanCount = 0;  // the fan is still in the array, but not used
    applyForces(t, none, 1.0f, wholeInterior(g));
    CHECK(totalMass(t.u) == 0.0);
    CHECK(totalMass(t.v) == 0.0);
}
