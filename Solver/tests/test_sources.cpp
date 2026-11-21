// The smoke source: += dt * rate inside a sphere (docs/plan/MATH.md, section 3).
#include "doctest/doctest.h"
#include "stages/Stages.h"
#include "test_helpers.h"

using smoke::GridSize;
using smoke::SmokeParams;
using smoke::SmokeState;
using namespace smoke_test;

TEST_CASE("sources_add_dt_times_rate_inside_the_sphere_only") {
    const GridSize g{16, 16, 16};
    SmokeState s;
    s.resize(g);

    SmokeParams p;
    p.source = {0.5f, 0.25f, 0.5f, 0.25f, 2.0f, 3.0f};  // centre (8.5, 4.5, 8.5), radius 4 cells
    const float dt = 0.5f;                              // so each cell gets exactly 1.0 and 1.5

    smoke::serial::addSources(s, p, dt, smoke::serial::wholeInterior(g));

    long long inside = 0, wrongInside = 0, wrongOutside = 0;
    for (int k = 1; k <= g.nz; ++k)
        for (int j = 1; j <= g.ny; ++j)
            for (int i = 1; i <= g.nx; ++i) {
                const float dx = i - 8.5f, dy = j - 4.5f, dz = k - 8.5f;
                const bool in = dx * dx + dy * dy + dz * dz <= 16.0f;
                inside += in;
                if (in) wrongInside += (s.density.at(i, j, k) != 1.0f) + (s.temperature.at(i, j, k) != 1.5f);
                else    wrongOutside += (s.density.at(i, j, k) != 0.0f) + (s.temperature.at(i, j, k) != 0.0f);
            }
    CHECK(inside > 0);
    CHECK(wrongInside == 0);
    CHECK(wrongOutside == 0);
    CHECK(totalMass(s.density) == doctest::Approx(double(inside)));
    // The phase keeps the ghost layer up to date (T5).
    CHECK(ghostLayerErrors(s.density, false, false, false) == 0);
}

TEST_CASE("sources_with_zero_radius_change_nothing") {
    SmokeState s;
    s.resize(GridSize{8, 8, 8});
    SmokeParams p;
    p.source = {0.5f, 0.5f, 0.5f, 0.0f, 5.0f, 5.0f};
    smoke::serial::addSources(s, p, 1.0f, smoke::serial::wholeInterior(GridSize{8, 8, 8}));
    CHECK(totalMass(s.density) == 0.0);
}

// D22: the extra sources add their own spheres; the same source twice gives twice as much.
TEST_CASE("D22 extra sources add separate spheres and add up where they overlap") {
    const GridSize g{16, 16, 16};
    SmokeState s;
    s.resize(g);
    SmokeParams p;
    p.source           = {0.25f, 0.25f, 0.25f, 0.125f, 2.0f, 2.0f};  // radius 2 cells
    p.extraSourceCount = 2;
    p.extraSources[0]  = {0.75f, 0.75f, 0.75f, 0.125f, 2.0f, 2.0f};
    p.extraSources[1]  = p.source;                                   // the first one again
    p.extraSources[2]  = {0.5f, 0.5f, 0.5f, 0.25f, 9.0f, 9.0f};      // beyond extraSourceCount: not used
    smoke::serial::addSources(s, p, 0.5f, smoke::serial::wholeInterior(g));

    CHECK(s.density.at(4, 4, 4) == 2.0f);    // centre (4.5, 4.5, 4.5): 2 * 0.5 * 2
    CHECK(s.density.at(12, 12, 12) == 1.0f); // centre (12.5, 12.5, 12.5): once
    CHECK(s.density.at(8, 8, 8) == 0.0f);    // the unused sphere would be here
    CHECK(ghostLayerErrors(s.density, false, false, false) == 0);

    SmokeState t;
    t.resize(g);
    SmokeParams onlyExtra = p;
    onlyExtra.source.radius = 0.0f;  // main source off, the extra ones still work
    smoke::serial::addSources(t, onlyExtra, 0.5f, smoke::serial::wholeInterior(g));
    CHECK(t.density.at(12, 12, 12) == 1.0f);
    CHECK(t.density.at(4, 4, 4) == 1.0f);
}
