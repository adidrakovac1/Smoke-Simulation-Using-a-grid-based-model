#include "doctest/doctest.h"
#include "smoke/SmokeSolver.h"

using smoke::GridSize;
using smoke::SmokeSolver;
using smoke::SolverConfig;

TEST_CASE("solver_default_grid_is_64_cubed") {
    SmokeSolver s;
    CHECK(s.gridSize() == (GridSize{64, 64, 64}));
    CHECK(s.stepCount() == 0);
}

TEST_CASE("solver_density_view_describes_interior") {
    SolverConfig cfg;
    cfg.grid = {32, 16, 8};
    SmokeSolver s(cfg);
    const smoke::DensityView v = s.density();
    CHECK(v.origin != nullptr);
    CHECK(v.nx == 32);
    CHECK(v.ny == 16);
    CHECK(v.nz == 8);
    CHECK(v.strideJ >= v.nx);            // padded rows
    CHECK(v.strideK == v.strideJ * (v.ny + 2));
    CHECK(v.origin[0] == 0.0f);          // empty at the start
}

TEST_CASE("solver_steps_count_and_record_timings") {
    SmokeSolver s;
    for (int i = 0; i < 3; ++i) s.step();
    CHECK(s.stepCount() == 3);
    CHECK(s.lastTimings().total() >= 0.0);
}

TEST_CASE("solver_resize_changes_grid_and_resets") {
    SmokeSolver s;
    s.step();
    s.resize(GridSize{32, 32, 32});
    CHECK(s.gridSize() == (GridSize{32, 32, 32}));
    CHECK(s.stepCount() == 0);
    CHECK(s.density().nx == 32);
    s.resize(GridSize{128, 128, 128});   // the larger size must also work
    CHECK(s.density().nz == 128);
}

TEST_CASE("solver_params_can_be_changed_while_running") {
    SmokeSolver s;
    s.params().jacobiIterations = 80;
    CHECK(s.params().jacobiIterations == 80);
    s.step();                            // must not crash with changed parameters
    CHECK(s.stepCount() == 1);
}
