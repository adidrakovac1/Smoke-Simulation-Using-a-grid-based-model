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
