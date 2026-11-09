// Trilinear interpolation and the clamp used by advection (docs/plan/MATH.md, section 4).
#include "doctest/doctest.h"
#include "stages/Interpolation.h"
#include "stages/Stages.h"
#include "test_helpers.h"

using smoke::Field3D;
using smoke::GridSize;
using smoke::serial::clampToBox;
using smoke::serial::sampleTrilinear;
using namespace smoke_test;

TEST_CASE("interpolation_at_a_cell_centre_returns_that_cell_exactly") {
    Field3D f(GridSize{7, 6, 5});
    SplitMix64 rng(3);
    fillRandom(f, rng, -5.0f, 5.0f);
    long long wrong = 0;
    for (int k = 1; k <= 5; ++k)
        for (int j = 1; j <= 6; ++j)
            for (int i = 1; i <= 7; ++i)
                wrong += sampleTrilinear(f, float(i), float(j), float(k)) != f.at(i, j, k);
    CHECK(wrong == 0);
}

TEST_CASE("interpolation_reproduces_a_linear_field") {
    // Trilinear interpolation is exact for f = a + b*x + c*y + d*z, so the only error is float rounding.
    // doctest::Approx uses a relative tolerance of 100 * FLT_EPSILON (about 1.2e-5) by default.
    const GridSize g{8, 9, 10};
    Field3D f(g);
    auto linear = [](float x, float y, float z) { return 1.0f + 2.0f * x - 3.0f * y + 0.5f * z; };
    for (int k = 0; k <= g.nz + 1; ++k)
        for (int j = 0; j <= g.ny + 1; ++j)
            for (int i = 0; i <= g.nx + 1; ++i) f.at(i, j, k) = linear(float(i), float(j), float(k));

    SplitMix64 rng(4);
    for (int n = 0; n < 1000; ++n) {
        const float x = rng.uniform(0.5f, g.nx + 0.5f);
        const float y = rng.uniform(0.5f, g.ny + 0.5f);
        const float z = rng.uniform(0.5f, g.nz + 0.5f);
        REQUIRE(sampleTrilinear(f, x, y, z) == doctest::Approx(linear(x, y, z)));
    }
}

TEST_CASE("clamp_keeps_the_point_between_the_walls") {
    CHECK(clampToBox(-3.0f, 10) == 0.5f);
    CHECK(clampToBox(0.5f, 10) == 0.5f);
    CHECK(clampToBox(4.25f, 10) == 4.25f);
    CHECK(clampToBox(10.5f, 10) == 10.5f);
    CHECK(clampToBox(99.0f, 10) == 10.5f);
}

TEST_CASE("raw_pointer_interpolation_is_bit_identical_to_the_m1_form") {
    // M4-012: sampleTrilinear(const float*, ...) must compute exactly what the original Field3D form did.
    const GridSize g{9, 7, 6};
    Field3D f(g);
    SplitMix64 rng(11);
    fillRandom(f, rng, -5.0f, 5.0f);
    int different = 0;
    for (int n = 0; n < 2000; ++n) {
        const float x = clampToBox(rng.uniform(-1.0f, g.nx + 2.0f), g.nx);  // includes clamped points
        const float y = clampToBox(rng.uniform(-1.0f, g.ny + 2.0f), g.ny);
        const float z = clampToBox(rng.uniform(-1.0f, g.nz + 2.0f), g.nz);
        const float a = sampleTrilinear(f, x, y, z), b = smoke::serial::sampleTrilinearM1(f, x, y, z);
        different += std::memcmp(&a, &b, sizeof(float)) != 0;
    }
    CHECK(different == 0);
}
