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
