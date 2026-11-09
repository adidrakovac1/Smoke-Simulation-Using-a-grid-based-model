#include "doctest/doctest.h"
#include "stages/Stages.h"
#include "test_helpers.h"

using smoke::GridSize;
using smoke::SmokeParams;
using smoke::SmokeState;
using namespace smoke::serial;
using namespace smoke_test;

TEST_CASE("vorticity_confinement_adds_eps_dt_N_cross_omega") {
    const GridSize g{10, 8, 8};
    SmokeState state;
    state.resize(g);
    for (int k = 1; k <= g.nz; ++k)
        for (int j = 1; j <= g.ny; ++j)
            for (int i = 1; i <= g.nx; ++i) state.v.at(i, j, k) = static_cast<float>(i * i);
    setVelocityBoundary(state);

    constexpr float epsilon = 0.5f;
    constexpr float dt = 0.25f;
    SmokeParams params;
    params.buoyancyAlpha = 0.0f;
    params.buoyancyBeta = 0.0f;
    params.vorticityEps = epsilon;
    applyForces(state, params, dt, wholeInterior(g));

    // At (4,4,4), omega=(0,0,8), grad|omega|=(2,0,0), so N x omega=(0,-8,0).
    CHECK(state.u.at(4, 4, 4) == 0.0f);
    CHECK(state.v.at(4, 4, 4) == doctest::Approx(15.0f));
    CHECK(state.w.at(4, 4, 4) == 0.0f);
    CHECK(ghostLayerErrors(state.u, true, false, false) == 0);
    CHECK(ghostLayerErrors(state.v, false, true, false) == 0);
    CHECK(ghostLayerErrors(state.w, false, false, true) == 0);
}

TEST_CASE("zero_vorticity_epsilon_leaves_velocity_unchanged") {
    const GridSize g{8, 8, 8};
    SmokeState state;
    state.resize(g);
    for (int k = 1; k <= g.nz; ++k)
        for (int j = 1; j <= g.ny; ++j)
            for (int i = 1; i <= g.nx; ++i) {
                state.u.at(i, j, k) = static_cast<float>(i * j);
                state.v.at(i, j, k) = static_cast<float>(j * k);
                state.w.at(i, j, k) = static_cast<float>(k * i);
            }
    setVelocityBoundary(state);
    const smoke::Field3D u = copyOf(state.u);
    const smoke::Field3D v = copyOf(state.v);
    const smoke::Field3D w = copyOf(state.w);

    applyVorticityConfinement(state, 0.0f, 0.1f, wholeInterior(g));

    CHECK(cellsDifferent(state.u, u) == 0);
    CHECK(cellsDifferent(state.v, v) == 0);
    CHECK(cellsDifferent(state.w, w) == 0);
}
