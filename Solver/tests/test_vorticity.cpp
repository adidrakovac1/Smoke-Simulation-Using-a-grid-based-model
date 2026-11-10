#include "doctest/doctest.h"
#include "stages/Stages.h"
#include "test_helpers.h"

#include <cmath>
#include <cstring>
#include <utility>

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

TEST_CASE("zero_vorticity_magnitude_gradient_leaves_local_velocity_unchanged") {
    const GridSize g{10, 10, 10};
    SmokeState state;
    state.resize(g);
    fillInterior(state.u, 2.0f);
    fillInterior(state.v, -3.0f);
    for (int k = 1; k <= g.nz; ++k)
        for (int j = 1; j <= g.ny; ++j)
            for (int i = 1; i <= g.nx; ++i) state.w.at(i, j, k) = static_cast<float>(j);
    setVelocityBoundary(state);

    constexpr int i = 5, j = 5, k = 5;
    const float u = state.u.at(i, j, k);
    const float v = state.v.at(i, j, k);
    const float w = state.w.at(i, j, k);
    applyVorticityConfinement(state, 5.0f, 0.1f, wholeInterior(g));

    CHECK(state.u.at(i, j, k) == u);
    CHECK(state.v.at(i, j, k) == v);
    CHECK(state.w.at(i, j, k) == w);
}

namespace {
// The vorticity confinement of M4-006 (Field3D::at, before the row kernels of M4-019), kept here as the
// reference for the bit-by-bit check below.
void vorticityConfinementM4006(SmokeState& s, float epsilon, float dt) {
    const GridSize g = s.u.size();
    for (int k = 1; k <= g.nz; ++k)
        for (int j = 1; j <= g.ny; ++j)
            for (int i = 1; i <= g.nx; ++i) {
                const float wx = 0.5f * ((s.w.at(i, j + 1, k) - s.w.at(i, j - 1, k)) -
                                         (s.v.at(i, j, k + 1) - s.v.at(i, j, k - 1)));
                const float wy = 0.5f * ((s.u.at(i, j, k + 1) - s.u.at(i, j, k - 1)) -
                                         (s.w.at(i + 1, j, k) - s.w.at(i - 1, j, k)));
                const float wz = 0.5f * ((s.v.at(i + 1, j, k) - s.v.at(i - 1, j, k)) -
                                         (s.u.at(i, j + 1, k) - s.u.at(i, j - 1, k)));
                s.vorticityX.at(i, j, k) = wx;
                s.vorticityY.at(i, j, k) = wy;
                s.vorticityZ.at(i, j, k) = wz;
                s.vorticityMagnitude.at(i, j, k) = std::sqrt(wx * wx + wy * wy + wz * wz);
            }
    setBoundary(s.vorticityMagnitude, BoundaryKind::Scalar);
    for (int k = 1; k <= g.nz; ++k)
        for (int j = 1; j <= g.ny; ++j)
            for (int i = 1; i <= g.nx; ++i) {
                const float nx = 0.5f * (s.vorticityMagnitude.at(i + 1, j, k) - s.vorticityMagnitude.at(i - 1, j, k));
                const float ny = 0.5f * (s.vorticityMagnitude.at(i, j + 1, k) - s.vorticityMagnitude.at(i, j - 1, k));
                const float nz = 0.5f * (s.vorticityMagnitude.at(i, j, k + 1) - s.vorticityMagnitude.at(i, j, k - 1));
                const float gradientMagnitude = std::sqrt(nx * nx + ny * ny + nz * nz);
                const float scale = epsilon * dt / (gradientMagnitude > 0.0f ? gradientMagnitude : 1.0f);
                const float wx = s.vorticityX.at(i, j, k);
                const float wy = s.vorticityY.at(i, j, k);
                const float wz = s.vorticityZ.at(i, j, k);
                s.u.at(i, j, k) += scale * (ny * wz - nz * wy);
                s.v.at(i, j, k) += scale * (nz * wx - nx * wz);
                s.w.at(i, j, k) += scale * (nx * wy - ny * wx);
            }
    setVelocityBoundary(s);
}
} // namespace

TEST_CASE("vorticity_row_kernels_are_bit_identical_to_the_m4_006_form") {
    // M4-019/M4-020: the row kernels (restrict, arithmetic mask instead of ?:, -fno-math-errno) and their AVX2
    // build must give exactly the M4-006 result. Random velocities, plus a block with zero velocity, where
    // |grad|w|| = 0 and the denominator must be exactly 1; nx = 13 covers the vector loop and its remainder.
    const GridSize g{13, 9, 7};
    SmokeState start;
    start.resize(g);
    SplitMix64 rng{123};
    fillRandom(start.u, rng, -3.0f, 3.0f);
    fillRandom(start.v, rng, -3.0f, 3.0f);
    fillRandom(start.w, rng, -3.0f, 3.0f);
    for (int k = 0; k <= 4; ++k)
        for (int j = 0; j <= 4; ++j)
            for (int i = 0; i <= 5; ++i) start.u.at(i, j, k) = start.v.at(i, j, k) = start.w.at(i, j, k) = 0.0f;
    setVelocityBoundary(start);

    auto copyStart = [&](SmokeState& dst) {  // Field3D is move-only: copy u, v, w (ghosts included) by memcpy
        dst.resize(g);
        for (auto [d, s] : {std::pair{&dst.u, &start.u}, std::pair{&dst.v, &start.v}, std::pair{&dst.w, &start.w}})
            std::memcpy(d->data(), s->data(), s->totalFloats() * sizeof(float));
    };
    SmokeState reference;
    copyStart(reference);
    vorticityConfinementM4006(reference, 5.0f, 1.0f / 60.0f);
    for (const KernelIsa isa : {KernelIsa::Scalar, KernelIsa::Avx2, KernelIsa::Auto}) {
        SmokeState s;
        copyStart(s);
        if (!applyVorticityConfinement(s, 5.0f, 1.0f / 60.0f, wholeInterior(g), isa)) {
            MESSAGE("AVX2 not available on this CPU/build: only the scalar path was checked");
            continue;
        }
        CHECK(cellsDifferent(s.u, reference.u) == 0);
        CHECK(cellsDifferent(s.v, reference.v) == 0);
        CHECK(cellsDifferent(s.w, reference.w) == 0);
        CHECK(cellsDifferent(s.vorticityMagnitude, reference.vorticityMagnitude) == 0);
    }
}
