#pragma once

#include "smoke/GridSize.h"
#include "smoke/SolverConfig.h"

namespace smokeviz {

/// The solver settings every smoke box in the visualization starts with, so the dedicated smoke scene and every
/// SmokeVolume (Terrain, the global Smoke menu) run the same, fastest version of the solver.
///
/// D19: the multi-threaded CPU backend (bit-identical to the serial one), one thread per physical core.
/// D20 (Ermin, 2026-11-17: lower CPI even if a step is a little slower): warm-started red-black SOR,
/// omega = 1.5, K = 6, and threads that sleep instead of spinning. Lowest CPI of all measured variants and a
/// smaller Poisson residual than the reference (Jacobi K = 40 from p = 0), still faster than D19.
/// Measured in docs/plan/PERF_LOG.md, P-005. The menu can switch the solver, omega, K and the warm start.
///
/// Sleeping threads also matter with several boxes (D21): each box owns its own thread team and the boxes are
/// stepped one after another, so with spinning threads the idle teams compete for the cores. Rough check on PC-2
/// (docs/plan/ARCHITECTURE.md, section 6, D21): with 2-3 boxes one box-step is ~1.1x slower than with one box
/// when threads sleep, but ~1.6-1.8x slower when they spin.
inline smoke::SolverConfig VisualizationSolverConfig(const smoke::GridSize& grid = {}) {
    smoke::SolverConfig config;
    config.grid       = grid;
    config.backend    = smoke::Backend::CpuParallel;
    config.threads    = 0;
    config.threadWait = smoke::ThreadWait::Block;
    config.params.pressureSolver    = smoke::PressureSolver::RedBlackGaussSeidel;
    config.params.sorOmega          = 1.5f;
    config.params.jacobiIterations  = 6;
    config.params.warmStartPressure = true;
    return config;
}

} // namespace smokeviz
