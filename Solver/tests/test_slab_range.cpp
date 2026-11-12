// SlabRange (docs/plan/ARCHITECTURE.md, section 5, rule 1; docs/plan/PLAN.md, M4c).
//
// Every kernel takes a range of z-planes [k0, k1). The serial solver always passes the whole interior, so
// nothing else in the test suite notices if a kernel ignores the range or reads something another slab was
// supposed to write. Here every kernel is called once for the whole interior and once in several pieces; the
// results must be bit-identical. That is the property a later threaded version relies on.
//
// What is NOT covered, on purpose: the composite stage functions (applyVorticityConfinement, advectVelocity,
// advectScalars, project). They also take a SlabRange, but they do whole-field work around the kernels
// (setBoundary, buffer swaps, p = 0), so they must be called once per step with the whole interior; a threaded
// version has to split the range INSIDE them, around the kernel calls (docs/plan/ARCHITECTURE.md, section 5).
#include "doctest/doctest.h"
#include "stages/Stages.h"
#include "test_helpers.h"

#include <vector>

using smoke::Field3D;
using smoke::GridSize;
using smoke::SmokeParams;
using smoke::SmokeState;
using namespace smoke::serial;
using namespace smoke_test;

namespace {

const GridSize kGrid{13, 10, 9};  // nx is not a multiple of 8, so the vector kernels also run their tail loop

// Pieces that cover the interior [1, nz + 1): uneven sizes, a one-plane slab and an empty slab.
std::vector<SlabRange> pieces(const GridSize& g) {
    return {{1, 4}, {4, 5}, {5, 5}, {5, g.nz + 1}};
}

Field3D randomField(std::uint64_t seed, float lo, float hi) {
    Field3D f(kGrid);
    SplitMix64 rng(seed);
    fillRandom(f, rng, lo, hi);
    return f;
}

void fillState(SmokeState& s, std::uint64_t seed) {
    s.resize(kGrid);
    SplitMix64 rng(seed);
    for (Field3D* f : s.fields()) fillRandom(*f, rng, -1.0f, 1.0f);
}

long long stateCellsDifferent(SmokeState& a, SmokeState& b) {
    long long n = 0;
    const auto fa = a.fields();
    const auto fb = b.fields();
    for (std::size_t i = 0; i < fa.size(); ++i) n += cellsDifferent(*fa[i], *fb[i]);
    return n;
}

} // namespace

TEST_CASE("slab_range_advection_in_pieces_is_bit_identical") {
    // Velocities up to 3 cells per step, so the back-trace reads planes far outside the slab being written.
    const Field3D u = randomField(1, -3.0f, 3.0f), v = randomField(2, -3.0f, 3.0f), w = randomField(3, -3.0f, 3.0f);
    const Field3D a = randomField(4, 0.0f, 1.0f), b = randomField(5, 0.0f, 1.0f), c = randomField(6, 0.0f, 1.0f);
    const Field3D* const src[3] = {&a, &b, &c};
    const Field3D start = randomField(7, -9.0f, 9.0f);  // what dst holds before; cells outside a slab must stay

    for (const KernelIsa isa : {KernelIsa::Scalar, KernelIsa::Auto}) {
        for (int count = 1; count <= 3; ++count) {
            Field3D whole[3] = {copyOf(start), copyOf(start), copyOf(start)};
            Field3D split[3] = {copyOf(start), copyOf(start), copyOf(start)};
            Field3D* const wholeDst[3] = {&whole[0], &whole[1], &whole[2]};
            Field3D* const splitDst[3] = {&split[0], &split[1], &split[2]};
            REQUIRE(advectFields(wholeDst, src, count, u, v, w, 1.0f, wholeInterior(kGrid), isa));
            for (const SlabRange piece : pieces(kGrid))
                REQUIRE(advectFields(splitDst, src, count, u, v, w, 1.0f, piece, isa));
            for (int n = 0; n < 3; ++n) CHECK(cellsDifferent(whole[n], split[n]) == 0);
        }
        Field3D whole = copyOf(start), split = copyOf(start);
        REQUIRE(advectScalar(whole, a, u, v, w, 1.0f, wholeInterior(kGrid), isa));
        for (const SlabRange piece : pieces(kGrid)) REQUIRE(advectScalar(split, a, u, v, w, 1.0f, piece, isa));
        CHECK(cellsDifferent(whole, split) == 0);
    }
}

TEST_CASE("slab_range_projection_kernels_in_pieces_are_bit_identical") {
    const Field3D u = randomField(11, -1.0f, 1.0f), v = randomField(12, -1.0f, 1.0f), w = randomField(13, -1.0f, 1.0f);
    const Field3D p = randomField(14, -1.0f, 1.0f);
    const Field3D start = randomField(15, -9.0f, 9.0f);

    SUBCASE("divergence") {
        Field3D whole = copyOf(start), split = copyOf(start);
        computeDivergence(u, v, w, whole, wholeInterior(kGrid));
        for (const SlabRange piece : pieces(kGrid)) computeDivergence(u, v, w, split, piece);
        CHECK(cellsDifferent(whole, split) == 0);
    }
    SUBCASE("jacobi iteration") {
        const Field3D div = randomField(16, -1.0f, 1.0f);
        for (const KernelIsa isa : {KernelIsa::Scalar, KernelIsa::Auto}) {
            Field3D whole = copyOf(start), split = copyOf(start);
            REQUIRE(jacobiIteration(whole, p, div, wholeInterior(kGrid), isa));
            for (const SlabRange piece : pieces(kGrid)) REQUIRE(jacobiIteration(split, p, div, piece, isa));
            CHECK(cellsDifferent(whole, split) == 0);
        }
    }
    SUBCASE("pressure gradient") {
        // In place on u, v, w, but it reads only p, so the pieces do not see each other's writes.
        Field3D wu = copyOf(u), wv = copyOf(v), ww = copyOf(w);
        Field3D su = copyOf(u), sv = copyOf(v), sw = copyOf(w);
        subtractPressureGradient(wu, wv, ww, p, wholeInterior(kGrid));
        for (const SlabRange piece : pieces(kGrid)) subtractPressureGradient(su, sv, sw, p, piece);
        CHECK(cellsDifferent(wu, su) == 0);
        CHECK(cellsDifferent(wv, sv) == 0);
        CHECK(cellsDifferent(ww, sw) == 0);
    }
}

TEST_CASE("slab_range_sources_and_buoyancy_in_pieces_are_bit_identical") {
    SmokeParams params;
    params.source.radius = 0.3f;  // large enough to span several planes of this small grid
    params.source.y = 0.5f;

    SmokeState whole, split;
    fillState(whole, 21);
    fillState(split, 21);
    REQUIRE(stateCellsDifferent(whole, split) == 0);

    addSources(whole, params, params.dt, wholeInterior(kGrid));
    for (const SlabRange piece : pieces(kGrid)) addSources(split, params, params.dt, piece);
    CHECK(stateCellsDifferent(whole, split) == 0);

    // Buoyancy changes v cell by cell from density and temperature, then refreshes the ghost layer of v.
    // Refreshing it after every piece is harmless (setBoundary only copies interior values outwards).
    applyBuoyancy(whole, params, params.dt, wholeInterior(kGrid));
    for (const SlabRange piece : pieces(kGrid)) applyBuoyancy(split, params, params.dt, piece);
    CHECK(stateCellsDifferent(whole, split) == 0);
}

TEST_CASE("slab_range_an_empty_slab_changes_nothing") {
    SmokeParams params;
    SmokeState state, before;
    fillState(state, 31);
    fillState(before, 31);
    for (Field3D* f : state.fields()) setBoundary(*f, BoundaryKind::Scalar);
    for (Field3D* f : before.fields()) setBoundary(*f, BoundaryKind::Scalar);
    const SlabRange empty{4, 4};

    addSources(state, params, params.dt, empty);
    computeDivergence(state.u, state.v, state.w, state.divergence, empty);
    REQUIRE(jacobiIteration(state.pressure, state.pressure0, state.divergence, empty));
    subtractPressureGradient(state.u, state.v, state.w, state.pressure, empty);
    REQUIRE(advectScalar(state.density, state.density0, state.u, state.v, state.w, params.dt, empty));
    CHECK(stateCellsDifferent(state, before) == 0);
}
