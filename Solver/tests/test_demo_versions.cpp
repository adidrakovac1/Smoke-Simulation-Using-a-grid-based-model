#include "DemoM3.h"
#include "SmokeState.h"
#include "doctest/doctest.h"
#include "smoke/SmokeSolver.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <vector>

namespace {

constexpr smoke::GridSize kGrid{16, 16, 16};
constexpr int kSteps = 8;

using PackedDensity = std::vector<float>;
using M3Step = void (*)(smoke::SmokeState&, const smoke::SmokeParams&, smoke::StepTimings&);

PackedDensity packInterior(const float* origin, int nx, int ny, int nz, std::ptrdiff_t strideJ,
                           std::ptrdiff_t strideK) {
    PackedDensity packed;
    packed.reserve(static_cast<std::size_t>(nx) * ny * nz);
    for (int k = 0; k < nz; ++k)
        for (int j = 0; j < ny; ++j)
            for (int i = 0; i < nx; ++i) packed.push_back(origin[i + j * strideJ + k * strideK]);
    return packed;
}

PackedDensity runM3(M3Step step) {
    smoke::SmokeState state;
    state.resize(kGrid);
    const smoke::SmokeParams params;
    smoke::StepTimings timings;
    for (int i = 0; i < kSteps; ++i) step(state, params, timings);

    const smoke::Field3D& density = state.density;
    return packInterior(&density.at(1, 1, 1), kGrid.nx, kGrid.ny, kGrid.nz, density.strideJ(),
                        density.strideK());
}

PackedDensity runM4() {
    smoke::SolverConfig config;
    config.grid = kGrid;
    smoke::SmokeSolver solver(config);
    for (int i = 0; i < kSteps; ++i) solver.step();

    const smoke::DensityView density = solver.density();
    return packInterior(density.origin, density.nx, density.ny, density.nz, density.strideJ, density.strideK);
}

bool allFinite(const PackedDensity& density) {
    return std::all_of(density.begin(), density.end(), [](float value) { return std::isfinite(value); });
}

std::size_t cellsDifferent(const PackedDensity& a, const PackedDensity& b) {
    if (a.size() != b.size()) return std::max(a.size(), b.size());
    std::size_t different = 0;
    for (std::size_t i = 0; i < a.size(); ++i)
        different += std::memcmp(&a[i], &b[i], sizeof(float)) != 0;
    return different;
}

} // namespace

TEST_CASE("demo_version_A_unoptimized_M3_is_repeatable_and_finite") {
    const PackedDensity first = runM3(smoke_demo::m3_b0::m3Step);
    const PackedDensity second = runM3(smoke_demo::m3_b0::m3Step);

    CHECK(allFinite(first));
    CHECK(std::any_of(first.begin(), first.end(), [](float value) { return value > 0.0f; }));
    CHECK(cellsDifferent(first, second) == 0);
}

TEST_CASE("demo_version_B_auto_vectorized_M3_matches_A") {
    const PackedDensity reference = runM3(smoke_demo::m3_b0::m3Step);
    const PackedDensity result = runM3(smoke_demo::m3_b1::m3Step);

    CHECK(allFinite(result));
    CHECK(cellsDifferent(reference, result) == 0);
}

TEST_CASE("demo_version_C_optimized_M4_matches_A") {
    const PackedDensity reference = runM3(smoke_demo::m3_b0::m3Step);
    const PackedDensity result = runM4();

    CHECK(allFinite(result));
    CHECK(cellsDifferent(reference, result) == 0);
}
