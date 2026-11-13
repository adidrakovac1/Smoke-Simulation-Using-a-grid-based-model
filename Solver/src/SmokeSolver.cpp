#include "smoke/SmokeSolver.h"

#include "DiagnosticsCompute.h"
#include "Profiler.h"
#include "SmokeState.h"
#include "stages/ObstacleLists.h"
#include "stages/Stages.h"

#include <utility>

namespace smoke {

struct SmokeSolver::Impl {
    SolverConfig config;
    SmokeState   state;
    StepTimings  timings;
    float        lastDt = 0.0f;   // dt of the last step, for the CFL number in diagnostics()
    long long    steps  = 0;
    // Null when there are no obstacles: then step() takes exactly the path it took before obstacles existed.
    std::unique_ptr<ObstacleLists> obstacles;

    explicit Impl(const SolverConfig& cfg) : config(cfg) { state.resize(config.grid); }
};

SmokeSolver::SmokeSolver(const SolverConfig& config) : impl_(std::make_unique<Impl>(config)) {}
SmokeSolver::~SmokeSolver()                                    = default;
SmokeSolver::SmokeSolver(SmokeSolver&&) noexcept               = default;
SmokeSolver& SmokeSolver::operator=(SmokeSolver&&) noexcept    = default;

void SmokeSolver::step(float dt) {
    using namespace serial;
    Impl& s = *impl_;
    const SlabRange all = wholeInterior(s.config.grid);

    s.timings = StepTimings{};  // timings of the last step only

    // Order of the phases follows docs/plan/MATH.md, section 2 (and the project plan document).
    // Obstacles (docs/plan/OBSTACLES.md): one pointer test per phase, outside every loop. Their cost is
    // counted in the phase they follow.
    const ObstacleLists* obstacles   = s.obstacles.get();
    const float          ambientTemp = s.config.params.ambientTemp;
    {
        ScopedTimer t(s.timings[Stage::Sources]);
        addSources(s.state, s.config.params, dt, all);
        if (obstacles) clearSolidScalars(s.state, ambientTemp, *obstacles);  // a source may overlap an obstacle
    }
    {
        ScopedTimer t(s.timings[Stage::Forces]);
        applyForces(s.state, s.config.params, dt, all);
        if (obstacles) {
            applyPorousDrag(s.state, dt, *obstacles);
            enforceSolidVelocity(s.state, *obstacles);
        }
    }
    {
        ScopedTimer t(s.timings[Stage::AdvectVelocity]);
        advectVelocity(s.state, dt, all);
        if (obstacles) enforceSolidVelocity(s.state, *obstacles);
    }
    {
        ScopedTimer t(s.timings[Stage::Project]);
        project(s.state, s.config.params, all, &s.timings.jacobiIterationMs, obstacles);
    }
    {
        ScopedTimer t(s.timings[Stage::AdvectScalars]);
        if (obstacles) {
            // A fluid cell next to an obstacle interpolates from the solid cells beside it. Give those cells
            // the value of the fluid, so smoke slides along the surface instead of being diluted by zeros.
            extendIntoSolids(s.state.density, *obstacles);
            extendIntoSolids(s.state.temperature, *obstacles);
        }
        advectScalars(s.state, dt, all);
        if (obstacles) {
            clearSolidScalars(s.state, ambientTemp, *obstacles);
            applyPorousFilter(s.state, dt, *obstacles);
        }
    }

    s.lastDt = dt;
    ++s.steps;
}

void SmokeSolver::reset() {
    impl_->state.clear();
    impl_->timings = StepTimings{};
    impl_->lastDt  = 0.0f;
    impl_->steps   = 0;
}

void SmokeSolver::resize(const GridSize& newSize) {
    impl_->config.grid = newSize;
    impl_->state.resize(newSize);
    impl_->obstacles.reset();  // they described the old grid
    impl_->timings = StepTimings{};
    impl_->lastDt  = 0.0f;
    impl_->steps   = 0;
}

const GridSize&    SmokeSolver::gridSize() const { return impl_->config.grid; }
SmokeParams&       SmokeSolver::params()         { return impl_->config.params; }
const SmokeParams& SmokeSolver::params() const   { return impl_->config.params; }
long long          SmokeSolver::stepCount() const { return impl_->steps; }

DensityView SmokeSolver::density() const {
    const Field3D& f = impl_->state.density;
    DensityView v;
    v.nx = f.size().nx;
    v.ny = f.size().ny;
    v.nz = f.size().nz;
    v.strideJ = f.strideJ();
    v.strideK = f.strideK();
    v.origin  = f.data() ? &f.at(1, 1, 1) : nullptr;  // first interior cell
    return v;
}

bool SmokeSolver::setObstacles(const ObstacleGrid& grid) {
    if (!(grid.size() == impl_->config.grid)) return false;
    if (grid.empty()) {
        impl_->obstacles.reset();
        return true;
    }
    impl_->obstacles = std::make_unique<ObstacleLists>(buildObstacleLists(grid, impl_->state.density));
    // Bring the current state in line with the new obstacles straight away.
    serial::enforceSolidVelocity(impl_->state, *impl_->obstacles);
    serial::clearSolidScalars(impl_->state, impl_->config.params.ambientTemp, *impl_->obstacles);
    return true;
}

void SmokeSolver::clearObstacles() { impl_->obstacles.reset(); }
bool SmokeSolver::hasObstacles() const { return impl_->obstacles != nullptr; }

Diagnostics SmokeSolver::diagnostics() const {
    return computeDiagnostics(impl_->state, impl_->lastDt, impl_->obstacles.get());
}
const StepTimings& SmokeSolver::lastTimings() const { return impl_->timings; }

} // namespace smoke
