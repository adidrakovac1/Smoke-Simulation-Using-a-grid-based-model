// T5 (boundary conditions), at the level of setBoundary itself. docs/plan/VERIFICATION.md, section 1.
#include "doctest/doctest.h"
#include "stages/Stages.h"
#include "test_helpers.h"

using smoke::Field3D;
using smoke::GridSize;
using smoke::serial::BoundaryKind;
using smoke::serial::setBoundary;
using namespace smoke_test;

namespace {
// A small non-cubic grid catches mixed-up nx/ny/nz.
const GridSize kGrid{6, 5, 4};
}

TEST_CASE("T5_boundary_scalar_copies_nearest_interior_cell_including_edges_and_corners") {
    Field3D f(kGrid);
    SplitMix64 rng(1);
    fillRandom(f, rng, -1.0f, 1.0f);  // ghost layer starts with garbage on purpose
    setBoundary(f, BoundaryKind::Scalar);
    CHECK(ghostLayerErrors(f, false, false, false) == 0);
}

TEST_CASE("T5_boundary_velocity_flips_only_the_normal_component") {
    SplitMix64 rng(2);
    Field3D u(kGrid), v(kGrid), w(kGrid);
    fillRandom(u, rng, -1.0f, 1.0f);
    fillRandom(v, rng, -1.0f, 1.0f);
    fillRandom(w, rng, -1.0f, 1.0f);
    setBoundary(u, BoundaryKind::VelocityX);
    setBoundary(v, BoundaryKind::VelocityY);
    setBoundary(w, BoundaryKind::VelocityZ);
    CHECK(ghostLayerErrors(u, true, false, false) == 0);
    CHECK(ghostLayerErrors(v, false, true, false) == 0);
    CHECK(ghostLayerErrors(w, false, false, true) == 0);

    // The physical meaning: the wall lies half-way between the ghost and the first interior cell, and the
    // normal velocity interpolated there is (ghost + interior) / 2 = 0, so nothing flows through the wall.
    long long leaks = 0;
    for (int k = 1; k <= kGrid.nz; ++k)
        for (int j = 1; j <= kGrid.ny; ++j) {
            leaks += (u.at(0, j, k) + u.at(1, j, k) != 0.0f);
            leaks += (u.at(kGrid.nx + 1, j, k) + u.at(kGrid.nx, j, k) != 0.0f);
        }
    CHECK(leaks == 0);
}
