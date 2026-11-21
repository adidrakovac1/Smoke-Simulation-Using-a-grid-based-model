#include "doctest/doctest.h"
#include "smoke/SmokeSolver.h"

#include "SmokeState.h"
#include "stages/ObstacleLists.h"
#include "stages/Stages.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
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

// The serial step as it was before decision D19 (SmokeSolver.cpp, commit 81a6949), written with the stage
// functions: the plain Jacobi loop, setBoundary on whole fields, applyVorticityConfinement in one call. The
// D19 step (whole-step team, slab boundaries, wavefront Jacobi) must give exactly the same bits.
void referenceStep(smoke::SmokeState& s, const smoke::SmokeParams& p, const smoke::ObstacleLists* obstacles, float dt) {
    using namespace smoke::serial;
    const SlabRange all = wholeInterior(s.density.size());
    addSources(s, p, dt, all);
    if (obstacles) clearSolidScalars(s, p.ambientTemp, *obstacles);

    applyBuoyancySlab(s, p, dt, all);
    setBoundary(s.v, BoundaryKind::VelocityY);
    if (p.vorticityEps > 0.0f) applyVorticityConfinement(s, p.vorticityEps, dt, all);
    if (obstacles) {
        applyPorousDrag(s, dt, *obstacles);
        enforceSolidVelocity(s, *obstacles);
    }

    advectVelocity(s, dt, all);
    if (obstacles) enforceSolidVelocity(s, *obstacles);

    project(s, p, all, nullptr, obstacles);

    if (obstacles) {
        extendIntoSolids(s.density, *obstacles);
        extendIntoSolids(s.temperature, *obstacles);
    }
    advectScalars(s, dt, all);
    if (obstacles) {
        clearSolidScalars(s, p.ambientTemp, *obstacles);
        applyPorousFilter(s, dt, *obstacles);
    }
}

bool sameBits(const smoke::Field3D& a, const smoke::DensityView& b) {
    const smoke::GridSize g = a.size();
    for (int k = 1; k <= g.nz; ++k)
        for (int j = 1; j <= g.ny; ++j)
            for (int i = 1; i <= g.nx; ++i) {
                const float va = a.at(i, j, k);
                const float vb = b.origin[(i - 1) + b.strideJ * (j - 1) + b.strideK * (k - 1)];
                if (std::memcmp(&va, &vb, sizeof(float)) != 0) return false;
            }
    return true;
}

smoke::ObstacleGrid makeObstacles(const smoke::GridSize& grid) {
    smoke::ObstacleGrid obstacles(grid);
    // A small solid block (several boundary cells, some touching each other) plus porous cells, one of them
    // next to a wall, so the wall/ghost-layer paths are used too.
    for (int k = grid.nz / 3; k <= grid.nz / 3 + 1; ++k)
        for (int j = grid.ny / 2 - 1; j <= grid.ny / 2; ++j)
            for (int i = grid.nx / 2 - 1; i <= grid.nx / 2 + 1; ++i) obstacles.set(i, j, k, smoke::CellType::Solid);
    obstacles.set(1, grid.ny / 2, grid.nz / 2 + 1, smoke::CellType::Solid);
    obstacles.set(grid.nx / 2, grid.ny / 2 + 2, grid.nz / 2, smoke::CellType::Porous);
    obstacles.set(grid.nx, 2, grid.nz, smoke::CellType::Porous);
    obstacles.porousDrag          = 0.75f;
    obstacles.porousDensityFilter = 0.25f;
    return obstacles;
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

// D20: the red-black solver and the warm start change the result, but the result must still not depend on the
// number of threads or on how the threads wait (spin or block).
TEST_CASE("D20 red-black and warm start: same bits for every thread count and wait mode") {
    for (const smoke::GridSize grid : {smoke::GridSize{20, 18, 22}, smoke::GridSize{33, 31, 29}}) {
        for (bool withObstacles : {false, true}) {
            for (int variant = 0; variant < 3; ++variant) {
                smoke::SolverConfig base;
                base.grid = grid;
                base.params.jacobiIterations = 7;
                // 0: red-black SOR, cold; 1: red-black GS, warm; 2: Jacobi, warm
                base.params.pressureSolver =
                    variant < 2 ? smoke::PressureSolver::RedBlackGaussSeidel : smoke::PressureSolver::Jacobi;
                base.params.sorOmega          = variant == 0 ? 1.7f : 1.0f;
                base.params.warmStartPressure = variant != 0;
                smoke::SmokeSolver serial(base);
                const smoke::ObstacleGrid obstacleGrid = makeObstacles(grid);
                if (withObstacles) REQUIRE(serial.setObstacles(obstacleGrid));
                for (int step = 0; step < 4; ++step) serial.step();
                CHECK(serial.diagnostics().nonFiniteCells == 0);
                for (int threads : {2, 3, 6}) {
                    for (smoke::ThreadWait wait : {smoke::ThreadWait::Spin, smoke::ThreadWait::Block}) {
                        smoke::SolverConfig config = base;
                        config.backend    = smoke::Backend::CpuParallel;
                        config.threads    = threads;
                        config.threadWait = wait;
                        smoke::SmokeSolver parallel(config);
                        if (withObstacles) REQUIRE(parallel.setObstacles(obstacleGrid));
                        for (int step = 0; step < 4; ++step) parallel.step();
                        INFO("variant " << variant << ", obstacles " << withObstacles << ", threads " << threads
                                        << ", block " << (wait == smoke::ThreadWait::Block));
                        CHECK(densityMatches(serial, parallel));
                    }
                }
            }
        }
    }
}

TEST_CASE("D20 blocking wait gives the reference result") {
    smoke::SolverConfig spin;
    spin.grid = {24, 20, 18};
    smoke::SolverConfig block = spin;
    spin.backend = block.backend = smoke::Backend::CpuParallel;
    spin.threads = block.threads = 4;
    block.threadWait = smoke::ThreadWait::Block;
    smoke::SmokeSolver a(spin), b(block), reference(smoke::SolverConfig{spin.grid, spin.params});
    for (int step = 0; step < 5; ++step) {
        a.step();
        b.step();
        reference.step();
    }
    CHECK(densityMatches(a, b));
    CHECK(densityMatches(reference, b));
}

// Largest | laplacian(p) - div | over the interior: how far p is from solving the pressure equation.
static double poissonResidual(const smoke::Field3D& p, const smoke::Field3D& div) {
    const smoke::GridSize g = p.size();
    double worst = 0.0;
    for (int k = 1; k <= g.nz; ++k)
        for (int j = 1; j <= g.ny; ++j)
            for (int i = 1; i <= g.nx; ++i) {
                const double lap = double(p.at(i - 1, j, k)) + p.at(i + 1, j, k) + p.at(i, j - 1, k) + p.at(i, j + 1, k) +
                                   p.at(i, j, k - 1) + p.at(i, j, k + 1) - 6.0 * p.at(i, j, k);
                worst = std::max(worst, std::fabs(lap - div.at(i, j, k)));
            }
    return worst;
}

TEST_CASE("D20 red-black converges faster than Jacobi, SOR faster still, warm start keeps a solution") {
    using namespace smoke::serial;
    const smoke::GridSize g{24, 24, 24};
    // A smooth divergence with zero mean (a solvable Neumann problem): a few sines.
    smoke::Field3D div(g), pJ(g), pJ0(g), pGS(g), pSOR(g);
    for (int k = 1; k <= g.nz; ++k)
        for (int j = 1; j <= g.ny; ++j)
            for (int i = 1; i <= g.nx; ++i)
                div.at(i, j, k) = 0.01f * std::cos(3.14159265f * (i - 0.5f) / g.nx) *
                                  std::cos(3.14159265f * (j - 0.5f) / g.ny) * std::cos(2.0f * 3.14159265f * (k - 0.5f) / g.nz);
    setBoundary(div, BoundaryKind::Scalar);
    const Team one{};
    const int K = 40;
    jacobiWavefront(pJ, pJ0, div, K, 0, nullptr, one);
    redBlackSolve(pGS, div, K, 1.0f, false, nullptr, one);
    redBlackSolve(pSOR, div, K, 1.8f, false, nullptr, one);
    // The exact solution (up to a constant, Neumann walls): many SOR iterations. Compare the ERROR, not the
    // largest residual: right after the black sweep the residual of red-black sits on the red cells only, so its
    // maximum says little (measured: Jacobi 0.0050, GS 0.0054, SOR 0.00028 after 40 iterations).
    smoke::Field3D exact(g);
    redBlackSolve(exact, div, 3000, 1.8f, false, nullptr, one);
    auto error = [&](const smoke::Field3D& p) {
        double meanP = 0.0, meanE = 0.0;
        for (int k = 1; k <= g.nz; ++k)
            for (int j = 1; j <= g.ny; ++j)
                for (int i = 1; i <= g.nx; ++i) {
                    meanP += p.at(i, j, k);
                    meanE += exact.at(i, j, k);
                }
        const double cells = double(g.nx) * g.ny * g.nz;
        meanP /= cells;
        meanE /= cells;
        double worst = 0.0;
        for (int k = 1; k <= g.nz; ++k)
            for (int j = 1; j <= g.ny; ++j)
                for (int i = 1; i <= g.nx; ++i)
                    worst = std::max(worst, std::fabs((p.at(i, j, k) - meanP) - (exact.at(i, j, k) - meanE)));
        return worst;
    };
    const double eJ = error(pJ), eGS = error(pGS), eSOR = error(pSOR);
    MESSAGE("max error after 40 iterations: Jacobi " << eJ << ", red-black GS " << eGS << ", SOR 1.8 " << eSOR
                                                     << " (exact residual " << poissonResidual(exact, div) << ")");
    CHECK(eGS < eJ);
    CHECK(eSOR < eGS);

    // Warm start from a (nearly) converged p: one more iteration must not make it worse.
    smoke::Field3D warm(g);
    redBlackSolve(warm, div, 400, 1.8f, false, nullptr, one);
    const double rBefore = poissonResidual(warm, div);
    redBlackSolve(warm, div, 1, 1.0f, true, nullptr, one);
    CHECK(poissonResidual(warm, div) <= rBefore * 1.0001 + 1e-9);
    // Warm start with 0 iterations leaves p untouched (cold start would reset it to 0).
    const float probe = warm.at(5, 6, 7);
    redBlackSolve(warm, div, 0, 1.0f, true, nullptr, one);
    CHECK(warm.at(5, 6, 7) == probe);
}

// D19: the whole-step team, the slab ghost layers and the wavefront Jacobi schedule against the pre-D19 serial
// step, for every shape of the schedule (wave depth D, planes per tick P), thread count, iteration count
// (0, 1, even, odd), with and without obstacles, and for grids whose planes do not split evenly.
TEST_CASE("D19 step matches the pre-D19 serial step bit for bit") {
    struct Case {
        smoke::GridSize grid;
        int             iterations;
    };
    const std::vector<Case> cases{{{16, 16, 1}, 5},  {{12, 10, 3}, 6},  {{9, 11, 7}, 1}, {{8, 10, 13}, 7},
                                  {{20, 18, 22}, 0}, {{20, 18, 22}, 9}, {{33, 31, 29}, 40}};
    const int shapes[][2] = {{0, 0}, {1, 1}, {3, 1}, {2, 2}, {4, 3}, {1, 100}, {5, 4}};
    for (const Case& c : cases) {
        for (bool withObstacles : {false, true}) {
            if (withObstacles && (c.grid.nx < 6 || c.grid.ny < 6 || c.grid.nz < 4)) continue;
            smoke::SmokeParams params;
            params.jacobiIterations = c.iterations;

            // Reference: the internal state driven by the pre-D19 step.
            smoke::SmokeState reference;
            reference.resize(c.grid);
            std::unique_ptr<smoke::ObstacleLists> lists;
            const smoke::ObstacleGrid obstacleGrid = makeObstacles(c.grid);
            if (withObstacles) {
                lists = std::make_unique<smoke::ObstacleLists>(smoke::buildObstacleLists(obstacleGrid, reference.density));
                smoke::serial::enforceSolidVelocity(reference, *lists);
                smoke::serial::clearSolidScalars(reference, params.ambientTemp, *lists);
            }
            constexpr int kSteps = 4;
            for (int step = 0; step < kSteps; ++step) referenceStep(reference, params, lists.get(), params.dt);

            for (int threads : {1, 2, 3, 6}) {
                for (const auto& shape : shapes) {
                    smoke::SolverConfig config;
                    config.grid                = c.grid;
                    config.params              = params;
                    config.backend             = threads == 1 ? smoke::Backend::Serial : smoke::Backend::CpuParallel;
                    config.threads             = threads;
                    config.jacobiWaveDepth     = shape[0];
                    config.jacobiPlanesPerTick = shape[1];
                    smoke::SmokeSolver solver(config);
                    if (withObstacles) REQUIRE(solver.setObstacles(obstacleGrid));
                    for (int step = 0; step < kSteps; ++step) solver.step();
                    INFO("grid " << c.grid.nx << "x" << c.grid.ny << "x" << c.grid.nz << ", K = " << c.iterations
                                 << ", obstacles " << withObstacles << ", threads " << threads << ", D = "
                                 << shape[0] << ", P = " << shape[1]);
                    CHECK(sameBits(reference.density, solver.density()));
                }
            }
        }
    }
}
