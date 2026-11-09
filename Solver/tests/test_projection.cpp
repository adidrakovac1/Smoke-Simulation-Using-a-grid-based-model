// Pressure projection: the pieces, and T3 (docs/plan/VERIFICATION.md, section 1).
#include "doctest/doctest.h"
#include "DiagnosticsCompute.h"
#include "stages/Stages.h"
#include "test_helpers.h"

#include <cmath>
#include <vector>

using smoke::Field3D;
using smoke::GridSize;
using smoke::SmokeParams;
using smoke::SmokeState;
using namespace smoke::serial;
using namespace smoke_test;

TEST_CASE("divergence_of_a_linear_field_is_its_constant_slope") {
    // u = i, v = -3 j, w = 0.5 k  =>  div = 1 - 3 + 0.5 = -1.5 everywhere (central differences are exact
    // for linear fields, and all numbers here are exact in float). Ghost cells are filled linearly too.
    const GridSize g{6, 5, 4};
    Field3D u(g), v(g), w(g), div(g);
    for (int k = 0; k <= g.nz + 1; ++k)
        for (int j = 0; j <= g.ny + 1; ++j)
            for (int i = 0; i <= g.nx + 1; ++i) {
                u.at(i, j, k) = float(i);
                v.at(i, j, k) = -3.0f * float(j);
                w.at(i, j, k) = 0.5f * float(k);
            }
    computeDivergence(u, v, w, div, wholeInterior(g));
    long long wrong = 0;
    for (int k = 1; k <= g.nz; ++k)
        for (int j = 1; j <= g.ny; ++j)
            for (int i = 1; i <= g.nx; ++i) wrong += div.at(i, j, k) != -1.5f;
    CHECK(wrong == 0);
}

TEST_CASE("pressure_gradient_is_subtracted_with_central_differences") {
    // p = 2 i + 4 k  =>  grad p = (2, 0, 4), so u -= 2, v unchanged, w -= 4.
    const GridSize g{5, 5, 5};
    Field3D u(g), v(g), w(g), p(g);
    for (int k = 0; k <= g.nz + 1; ++k)
        for (int j = 0; j <= g.ny + 1; ++j)
            for (int i = 0; i <= g.nx + 1; ++i) p.at(i, j, k) = 2.0f * float(i) + 4.0f * float(k);
    subtractPressureGradient(u, v, w, p, wholeInterior(g));
    CHECK(u.at(3, 3, 3) == -2.0f);
    CHECK(v.at(3, 3, 3) == 0.0f);
    CHECK(w.at(3, 3, 3) == -4.0f);
}

TEST_CASE("jacobi_iteration_averages_the_six_neighbours_minus_divergence") {
    const GridSize g{4, 4, 4};
    Field3D pOld(g), pNew(g), div(g);
    pOld.at(1, 2, 2) = 1.0f;  pOld.at(3, 2, 2) = 2.0f;
    pOld.at(2, 1, 2) = 3.0f;  pOld.at(2, 3, 2) = 4.0f;
    pOld.at(2, 2, 1) = 5.0f;  pOld.at(2, 2, 3) = 6.0f;
    div.at(2, 2, 2) = 3.0f;
    jacobiIteration(pNew, pOld, div, wholeInterior(g));
    CHECK(pNew.at(2, 2, 2) == 3.0f);  // (21 - 3) / 6
}

TEST_CASE("jacobi_avx2_and_scalar_builds_are_bit_identical") {
    // M4-009: the AVX2 build of the Jacobi kernel (Projection_avx2.cpp, -mavx2, no FMA) must give exactly
    // the scalar result. nx = 13 is not a multiple of 8, so the vector loop AND its remainder are exercised.
    const GridSize g{13, 7, 5};
    Field3D pOld(g), div(g), pScalar(g), pAvx2(g);
    SplitMix64 rng{2026};
    fillRandom(pOld, rng, -1.0f, 1.0f);
    fillRandom(div, rng, -1.0f, 1.0f);

    REQUIRE(jacobiIteration(pScalar, pOld, div, wholeInterior(g), KernelIsa::Scalar));
    if (!jacobiIteration(pAvx2, pOld, div, wholeInterior(g), KernelIsa::Avx2)) {
        MESSAGE("AVX2 not available on this CPU/build: only the scalar path was checked");
        return;
    }
    CHECK(cellsDifferent(pScalar, pAvx2) == 0);

    Field3D pAuto(g);  // Auto must pick AVX2 here and therefore also match
    jacobiIteration(pAuto, pOld, div, wholeInterior(g));
    CHECK(cellsDifferent(pScalar, pAuto) == 0);
}

namespace {

// Random velocity in [-1, 1) with a fixed seed (T3 in docs/plan/VERIFICATION.md), projected with K Jacobi
// iterations. Returns max|div| after / max|div| before.
double projectionRatio(const GridSize& g, int iterations) {
    SmokeState s;
    s.resize(g);
    SplitMix64 rng(42);
    fillRandom(s.u, rng, -1.0f, 1.0f);
    fillRandom(s.v, rng, -1.0f, 1.0f);
    fillRandom(s.w, rng, -1.0f, 1.0f);
    setVelocityBoundary(s);
    SmokeParams p;
    p.jacobiIterations = iterations;
    project(s, p, wholeInterior(g));
    const smoke::Diagnostics d = smoke::computeDiagnostics(s, 0.0f);
    return d.maxDivAfter / d.maxDivBefore;
}

} // namespace

TEST_CASE("T3_projection_reduces_divergence_and_more_iterations_reduce_it_more") {
    const std::vector<int> iterations{10, 20, 40, 80};
    for (const GridSize g : {GridSize{32, 32, 32}, GridSize{64, 64, 64}}) {
        std::vector<double> ratios;
        for (int k : iterations) ratios.push_back(projectionRatio(g, k));
        MESSAGE("T3 measured on " << g.nx << "^3: ratio after/before for K = 10, 20, 40, 80: " << ratios[0]
                                  << ", " << ratios[1] << ", " << ratios[2] << ", " << ratios[3]);
        for (std::size_t n = 1; n < ratios.size(); ++n) CHECK(ratios[n] < ratios[n - 1]);
        // Threshold from measurement (2026-11-09, g++ 16.2.0 Release, seed 42): with K = 80 the ratio is
        // 0.710 on 32^3 and 0.736 on 64^3. It stays high for white noise because on a cell-centred grid the
        // checkerboard part of a random field cannot be removed (docs/plan/MATH.md, section 5, note on
        // accuracy). Limit 0.8. A wrong sign in the Jacobi step gives 1.38 to 1.45, growing with K.
        CHECK(ratios.back() < 0.8);
    }
}

TEST_CASE("T5_projection_keeps_velocity_and_pressure_ghost_layers_up_to_date") {
    const GridSize g{12, 10, 8};
    SmokeState s;
    s.resize(g);
    SplitMix64 rng(9);
    fillRandom(s.u, rng, -1.0f, 1.0f);
    fillRandom(s.v, rng, -1.0f, 1.0f);
    fillRandom(s.w, rng, -1.0f, 1.0f);
    setVelocityBoundary(s);
    project(s, SmokeParams{}, wholeInterior(g));
    CHECK(ghostLayerErrors(s.u, true, false, false) == 0);
    CHECK(ghostLayerErrors(s.v, false, true, false) == 0);
    CHECK(ghostLayerErrors(s.w, false, false, true) == 0);
    CHECK(ghostLayerErrors(s.pressure, false, false, false) == 0);
}

namespace {

// A smooth, purely divergent field: u = grad(phi) with phi a Gaussian bump in the middle of the box.
// Its divergence is smooth, so the difference between the wide (gradient-then-divergence) and the compact
// (7-point Jacobi) Laplacian is small, and with enough iterations the projection must remove almost all of
// it. A wrong sign or scale in div / Jacobi / gradient would make the ratio grow instead.
double smoothProjectionRatio(const GridSize& g, int iterations) {
    SmokeState s;
    s.resize(g);
    const float cx = 0.5f * float(g.nx + 1), cy = 0.5f * float(g.ny + 1), cz = 0.5f * float(g.nz + 1);
    const float sigma2 = (0.15f * float(g.nx)) * (0.15f * float(g.nx));
    for (int k = 1; k <= g.nz; ++k)
        for (int j = 1; j <= g.ny; ++j)
            for (int i = 1; i <= g.nx; ++i) {
                const float dx = float(i) - cx, dy = float(j) - cy, dz = float(k) - cz;
                const float phi = std::exp(-(dx * dx + dy * dy + dz * dz) / (2.0f * sigma2));
                s.u.at(i, j, k) = -dx / sigma2 * phi;
                s.v.at(i, j, k) = -dy / sigma2 * phi;
                s.w.at(i, j, k) = -dz / sigma2 * phi;
            }
    setVelocityBoundary(s);
    SmokeParams p;
    p.jacobiIterations = iterations;
    project(s, p, wholeInterior(g));
    const smoke::Diagnostics d = smoke::computeDiagnostics(s, 0.0f);
    return d.maxDivAfter / d.maxDivBefore;
}

} // namespace

TEST_CASE("projection_removes_almost_all_divergence_of_a_smooth_field_when_converged") {
    const GridSize g{16, 16, 16};
    const double r10 = smoothProjectionRatio(g, 10), r100 = smoothProjectionRatio(g, 100);
    const double r1000 = smoothProjectionRatio(g, 1000), r5000 = smoothProjectionRatio(g, 5000);
    MESSAGE("smooth field 16^3: ratio after/before for K = 10, 100, 1000, 5000: " << r10 << ", " << r100 << ", "
                                                                               << r1000 << ", " << r5000);
    CHECK(r100 < r10);
    CHECK(r1000 < r100);
    CHECK(r5000 <= r1000);
    // Measured 2026-11-09 (g++ 16.2.0 Release): 0.421, 0.117, 0.111, 0.111. The converged value 0.111 is the
    // floor set by the wide-versus-compact Laplacian difference (docs/plan/MATH.md, section 5). Limit 0.15.
    // A wrong sign in the Jacobi step gives 1.58 to 1.89.
    CHECK(r1000 < 0.15);
}
