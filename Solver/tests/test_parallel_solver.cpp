#include "doctest/doctest.h"
#include "smoke/SmokeSolver.h"

#include <cstring>
#include <vector>

namespace {

bool densityMatches(const smoke::SmokeSolver& a, const smoke::SmokeSolver& b) {
    const auto da = a.density();
    const auto db = b.density();
    if (da.nx != db.nx || da.ny != db.ny || da.nz != db.nz) return false;
    for (int k = 0; k < da.nz; ++k)
        for (int j = 0; j < da.ny; ++j)
            for (int i = 0; i < da.nx; ++i) {
                const float va = da.origin[i + da.strideJ * j + da.strideK * k];
                const float vb = db.origin[i + db.strideJ * j + db.strideK * k];
                if (std::memcmp(&va, &vb, sizeof(float)) != 0) return false;
            }
    return true;
}

} // namespace

TEST_CASE("solver defaults to serial and parallel backend matches serial bit for bit") {
    CHECK(smoke::SolverConfig{}.backend == smoke::Backend::Serial);

    const std::vector<smoke::GridSize> grids{{16, 16, 1}, {12, 10, 3}, {9, 11, 7}, {8, 10, 13},
                                             {16, 16, 16}, {32, 32, 32}, {64, 64, 64},
                                             {96, 96, 96}, {128, 128, 128}};
    for (const auto& grid : grids) {
        for (int threads : {2, 3, 5}) {
            smoke::SolverConfig serialConfig;
            serialConfig.grid = grid;
            serialConfig.params.jacobiIterations = 6;
            smoke::SolverConfig parallelConfig = serialConfig;
            parallelConfig.backend = smoke::Backend::CpuParallel;
            parallelConfig.threads = threads;

            smoke::SmokeSolver serial(serialConfig);
            smoke::SmokeSolver parallel(parallelConfig);
            smoke::ObstacleGrid obstacles(grid);
            if (grid.nx > 4 && grid.ny > 4 && grid.nz > 1) {
                obstacles.set(grid.nx / 2, grid.ny / 2, grid.nz / 2, smoke::CellType::Solid);
                obstacles.set(grid.nx / 2 + 1, grid.ny / 2, grid.nz / 2, smoke::CellType::Porous);
                obstacles.porousDrag = 0.75f;
                obstacles.porousDensityFilter = 0.25f;
                REQUIRE(serial.setObstacles(obstacles));
                REQUIRE(parallel.setObstacles(obstacles));
            }

            for (int step = 0; step < 3; ++step) {
                serial.step();
                parallel.step();
            }
            CHECK(parallel.threadCount() == threads);
            CHECK(densityMatches(serial, parallel));
        }
    }
}
