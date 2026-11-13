#pragma once

#include "smoke/DensityView.h"
#include "smoke/Diagnostics.h"
#include "smoke/GridSize.h"
#include "smoke/Obstacles.h"
#include "smoke/SmokeParams.h"
#include "smoke/SolverConfig.h"
#include "smoke/StepTimings.h"

#include <memory>

namespace smoke {

/// The single class the rest of the project (console program, tests, later the renderer
/// integration) talks to. Everything else in Solver/src is an implementation detail.
///
/// The simulation lives in an "invisible box": a grid of cells with solid walls. Nothing about
/// the box's position in a 3D scene is known here; the renderer side places it later
/// (docs/plan/ARCHITECTURE.md, section 8).
class SmokeSolver {
public:
    explicit SmokeSolver(const SolverConfig& config = {});
    ~SmokeSolver();

    SmokeSolver(const SmokeSolver&)            = delete;
    SmokeSolver& operator=(const SmokeSolver&) = delete;
    SmokeSolver(SmokeSolver&&) noexcept;
    SmokeSolver& operator=(SmokeSolver&&) noexcept;

    /// Advance the simulation by dt seconds (one step).
    void step(float dt);
    /// Same, using params().dt.
    void step() { step(params().dt); }

    /// Clear all fields (grid size stays).
    void reset();
    /// Reallocate everything for a new grid size and reset. Not cheap: do not call every frame.
    void resize(const GridSize& newSize);

    const GridSize&    gridSize() const;
    SmokeParams&       params();
    const SmokeParams& params() const;
    long long          stepCount() const;

    /// Puts obstacles into the box (docs/plan/OBSTACLES.md). The cell types are copied and turned into the
    /// solver's sparse lists once, here. The grid must have the same size as gridSize(); otherwise nothing
    /// changes and false is returned. A grid with only Fluid cells is the same as clearObstacles().
    /// resize() removes the obstacles, because they described the old grid.
    bool setObstacles(const ObstacleGrid& grid);
    /// Removes all obstacles. Without obstacles the result is bit for bit the same as before they existed.
    void clearObstacles();
    bool hasObstacles() const;

    DensityView          density() const;
    /// Computes the health numbers of the current state ON DEMAND (several passes over the grid).
    /// Not part of step() and not included in lastTimings(), so measurements stay clean.
    Diagnostics          diagnostics() const;
    const StepTimings&   lastTimings() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace smoke
