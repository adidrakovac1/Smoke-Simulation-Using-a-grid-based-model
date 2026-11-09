// Whole-step tests: T5 (all ghost layers after a full step), T8 (no NaN/Inf), T9 (resize) and the
// on-demand diagnostics (docs/plan/VERIFICATION.md, sections 1 and 2).
#include "doctest/doctest.h"
#include "DiagnosticsCompute.h"
#include "smoke/SmokeSolver.h"
#include "stages/Stages.h"
#include "test_helpers.h"

#include <limits>

using smoke::GridSize;
using smoke::SmokeParams;
using smoke::SmokeSolver;
using smoke::SmokeState;
using smoke::SolverConfig;
using namespace smoke::serial;
using namespace smoke_test;

TEST_CASE("T5_full_step_leaves_every_ghost_layer_up_to_date") {
    // The five phases called in step() order on a small grid, with the default (tuned) parameters.
    const GridSize g{16, 20, 12};
    SmokeState s;
    s.resize(g);
    const SmokeParams p;
    for (int n = 0; n < 5; ++n) {
        addSources(s, p, p.dt, wholeInterior(g));
        applyForces(s, p, p.dt, wholeInterior(g));
        advectVelocity(s, p.dt, wholeInterior(g));
        project(s, p, wholeInterior(g));
        advectScalars(s, p.dt, wholeInterior(g));
    }
    CHECK(totalMass(s.density) > 0.0);  // something actually happened
    CHECK(ghostLayerErrors(s.density, false, false, false) == 0);
    CHECK(ghostLayerErrors(s.temperature, false, false, false) == 0);
    CHECK(ghostLayerErrors(s.pressure, false, false, false) == 0);
    CHECK(ghostLayerErrors(s.u, true, false, false) == 0);
    CHECK(ghostLayerErrors(s.v, false, true, false) == 0);
    CHECK(ghostLayerErrors(s.w, false, false, true) == 0);
}

TEST_CASE("T8_500_steps_with_default_parameters_stay_finite") {
    // 32^3 keeps the test suite fast; the 64^3 run is part of the M2 criterion through smoke_cli.
    SolverConfig cfg;
    cfg.grid = {32, 32, 32};
    SmokeSolver solver(cfg);
    for (int n = 0; n < 500; ++n) solver.step();
    const smoke::Diagnostics d = solver.diagnostics();
    CHECK(d.computed);
    CHECK(d.nonFiniteCells == 0);
    CHECK(d.totalMass > 0.0);
    CHECK(d.maxSpeed > 0.0);  // buoyancy moved the air
}

TEST_CASE("T9_resize_reallocates_and_clears_every_field") {
    SolverConfig cfg;  // default 64^3 and default (tuned) source, so the state is not empty before resizing
    SmokeSolver solver(cfg);
    for (int n = 0; n < 5; ++n) solver.step();
    REQUIRE(solver.diagnostics().totalMass > 0.0);

    for (const GridSize g : {GridSize{32, 32, 32}, GridSize{128, 128, 128}}) {
        solver.resize(g);
        CHECK(solver.gridSize() == g);
        CHECK(solver.stepCount() == 0);
        const smoke::DensityView v = solver.density();
        CHECK(v.nx == g.nx);
        CHECK(v.nz == g.nz);
        const smoke::Diagnostics d = solver.diagnostics();
        CHECK(d.totalMass == 0.0);     // density cleared
        CHECK(d.maxSpeed == 0.0);      // velocity cleared
        CHECK(d.maxDivBefore == 0.0);  // divergence buffer cleared
        CHECK(d.nonFiniteCells == 0);
    }
    solver.step();  // the new size works
    CHECK(solver.stepCount() == 1);
}

TEST_CASE("diagnostics_report_mass_speed_and_nan_count") {
    const GridSize g{8, 8, 8};
    SmokeState s;
    s.resize(g);
    s.density.at(2, 3, 4) = 1.5f;
    s.density.at(5, 5, 5) = 2.5f;
    s.u.at(3, 3, 3) = 3.0f;
    s.v.at(3, 3, 3) = 4.0f;  // |u| = 5 in that cell
    s.temperature.at(1, 1, 1) = std::numeric_limits<float>::quiet_NaN();

    const smoke::Diagnostics d = smoke::computeDiagnostics(s, 0.1f);
    CHECK(d.computed);
    CHECK(d.totalMass == 4.0);
    CHECK(d.maxSpeed == doctest::Approx(5.0));
    CHECK(d.cfl == doctest::Approx(0.5));
    CHECK(d.nonFiniteCells == 1);
}
