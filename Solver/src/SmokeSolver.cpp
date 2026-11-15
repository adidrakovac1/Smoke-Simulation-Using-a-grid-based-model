#include "smoke/SmokeSolver.h"

#include "DiagnosticsCompute.h"
#include "Profiler.h"
#include "SmokeState.h"
#include "ThreadPool.h"
#include "stages/ObstacleLists.h"
#include "stages/Stages.h"

#include <barrier>
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
    std::unique_ptr<ThreadPool>    pool;

    explicit Impl(const SolverConfig& cfg) : config(cfg) {
        state.resize(config.grid);
        if (config.backend == Backend::CpuParallel && config.threads != 1) {
            pool = std::make_unique<ThreadPool>(config.threads);
        }
    }
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

    const ObstacleLists* obstacles   = s.obstacles.get();
    const float          ambientTemp = s.config.params.ambientTemp;

    // Use parallel path when a thread pool is active with >1 threads
    if (s.pool && s.pool->numThreads() > 1 && s.config.grid.nz > 1) {
        ThreadPool& pool = *s.pool;
        const GridSize g = s.config.grid;

        // 1. Sources
        {
            ScopedTimer t(s.timings[Stage::Sources]);
            addSources(s.state, s.config.params, dt, all);
            if (obstacles) clearSolidScalars(s.state, ambientTemp, *obstacles);
        }

        // 2. Forces
        {
            ScopedTimer t(s.timings[Stage::Forces]);
            pool.parallelSlabs(g.nz, [&](SlabRange slab, int) {
                applyBuoyancy(s.state, s.config.params, dt, slab);
            });
            setBoundary(s.state.v, BoundaryKind::VelocityY);

            if (s.config.params.vorticityEps > 0.0f) {
                applyVorticityConfinement(s.state, s.config.params.vorticityEps, dt, all);
            }

            if (obstacles) {
                applyPorousDrag(s.state, dt, *obstacles);
                enforceSolidVelocity(s.state, *obstacles);
            }
        }

        // 3. Advect Velocity
        {
            ScopedTimer t(s.timings[Stage::AdvectVelocity]);
            s.state.u.swap(s.state.u0);
            s.state.v.swap(s.state.v0);
            s.state.w.swap(s.state.w0);
            Field3D* const dstV[3] = {&s.state.u, &s.state.v, &s.state.w};
            const Field3D* const srcV[3] = {&s.state.u0, &s.state.v0, &s.state.w0};
            pool.parallelSlabs(g.nz, [&](SlabRange slab, int) {
                advectFields(dstV, srcV, 3, s.state.u0, s.state.v0, s.state.w0, dt, slab);
            });
            setVelocityBoundary(s.state);
            if (obstacles) enforceSolidVelocity(s.state, *obstacles);
        }

        // 4. Project
        {
            ScopedTimer t(s.timings[Stage::Project]);
            pool.parallelSlabs(g.nz, [&](SlabRange slab, int) {
                computeDivergence(s.state.u, s.state.v, s.state.w, s.state.divergence, slab);
            });
            setBoundary(s.state.divergence, BoundaryKind::Scalar);

            s.state.pressure.fill(0.0f);
            s.state.pressure0.fill(0.0f);
            const int iterations = s.config.params.jacobiIterations;
            if (iterations > 0) {
                double jacobiTotalMs = 0.0;
                {
                    ScopedTimer jt(jacobiTotalMs);
                    auto on_complete = [&]() noexcept {
                        if (obstacles) extendIntoSolids(s.state.pressure, *obstacles);
                        setBoundary(s.state.pressure, BoundaryKind::Scalar);
                        s.state.pressure.swap(s.state.pressure0);
                    };
                    std::barrier sync(pool.numThreads(), on_complete);

                    pool.parallelRun([&](int tid) {
                        const int k0 = 1 + (tid * g.nz) / pool.numThreads();
                        const int k1 = 1 + ((tid + 1) * g.nz) / pool.numThreads();
                        const SlabRange slab{k0, k1};
                        for (int it = 0; it < iterations; ++it) {
                            if (slab.k0 < slab.k1) {
                                jacobiIteration(s.state.pressure, s.state.pressure0, s.state.divergence, slab);
                            }
                            sync.arrive_and_wait();
                        }
                    });
                    s.state.pressure.swap(s.state.pressure0);
                }
                s.timings.jacobiIterationMs = jacobiTotalMs / iterations;
            } else {
                s.timings.jacobiIterationMs = 0.0;
            }

            pool.parallelSlabs(g.nz, [&](SlabRange slab, int) {
                subtractPressureGradient(s.state.u, s.state.v, s.state.w, s.state.pressure, slab);
            });
            if (obstacles) reflectVelocityIntoSolids(s.state.u, s.state.v, s.state.w, *obstacles);
            setVelocityBoundary(s.state);
        }

        // 5. Advect Scalars
        {
            ScopedTimer t(s.timings[Stage::AdvectScalars]);
            if (obstacles) {
                extendIntoSolids(s.state.density, *obstacles);
                extendIntoSolids(s.state.temperature, *obstacles);
            }
            s.state.density.swap(s.state.density0);
            s.state.temperature.swap(s.state.temperature0);
            Field3D* const dstS[2] = {&s.state.density, &s.state.temperature};
            const Field3D* const srcS[2] = {&s.state.density0, &s.state.temperature0};
            pool.parallelSlabs(g.nz, [&](SlabRange slab, int) {
                advectFields(dstS, srcS, 2, s.state.u, s.state.v, s.state.w, dt, slab);
            });
            setBoundary(s.state.density, BoundaryKind::Scalar);
            setBoundary(s.state.temperature, BoundaryKind::Scalar);
            if (obstacles) {
                clearSolidScalars(s.state, ambientTemp, *obstacles);
                applyPorousFilter(s.state, dt, *obstacles);
            }
        }
    } else {
        // Serial execution path
        {
            ScopedTimer t(s.timings[Stage::Sources]);
            addSources(s.state, s.config.params, dt, all);
            if (obstacles) clearSolidScalars(s.state, ambientTemp, *obstacles);
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
                extendIntoSolids(s.state.density, *obstacles);
                extendIntoSolids(s.state.temperature, *obstacles);
            }
            advectScalars(s.state, dt, all);
            if (obstacles) {
                clearSolidScalars(s.state, ambientTemp, *obstacles);
                applyPorousFilter(s.state, dt, *obstacles);
            }
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
int                SmokeSolver::threadCount() const { return impl_->pool ? impl_->pool->numThreads() : 1; }

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
