#include "smoke/SmokeSolver.h"

#include "DiagnosticsCompute.h"
#include "Profiler.h"
#include "SmokeState.h"
#include "ThreadPool.h"
#include "stages/ObstacleLists.h"
#include "stages/Stages.h"

#include <chrono>
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
            pool = std::make_unique<ThreadPool>(config.threads, config.threadWait == ThreadWait::Block);
        }
    }
};

SmokeSolver::SmokeSolver(const SolverConfig& config) : impl_(std::make_unique<Impl>(config)) {}
SmokeSolver::~SmokeSolver()                                    = default;
SmokeSolver::SmokeSolver(SmokeSolver&&) noexcept               = default;
SmokeSolver& SmokeSolver::operator=(SmokeSolver&&) noexcept    = default;

namespace {

// One simulation step, run by every thread of `team` (decision D19). The phases, and the order of the calls
// inside each phase, are those of the serial reference step before D19 (git history of this file; the stage
// functions in Stages.h, e.g. project()); every kernel computes every cell with the same arithmetic, so the
// result is bit-identical for any number of threads.
//
// How the work is split:
//   * The cell kernels work on z-slabs: thread tid owns the planes ThreadPool::slabOf(nz, tid, size), writes
//     only those planes, and refreshes their ghost cells itself (setBoundarySlab).
//   * team.sync() (a spin barrier) separates two kernels when the second one reads cells of other slabs
//     written by the first one (neighbours in k, or anywhere for the advection back-trace).
//   * The obstacle passes walk short lists in a fixed order; they run on thread 0 between two barriers
//     ("serialPart"), exactly where the reference step runs them.
//   * Jacobi uses the wavefront schedule (jacobiWavefront), split by rows j.
// Thread 0 also takes the time of every phase; each phase ends with a barrier, so that is the wall time of
// the whole team.
void runStep(SmokeState& st, const SmokeParams& params, const ObstacleLists* obstacles, int waveDepth,
             int planesPerTick, float dt, const serial::Team& team, StepTimings& timings) {
    using namespace serial;
    using Clock = std::chrono::steady_clock;
    const GridSize  g      = st.density.size();
    const SlabRange slab   = ThreadPool::slabOf(g.nz, team.tid, team.size);
    const bool      mine   = slab.k0 < slab.k1;
    const bool      timing = team.tid == 0;
    auto serialPart = [&](auto&& work) {
        if (team.tid == 0) work();
        team.sync();
    };
    Clock::time_point mark = timing ? Clock::now() : Clock::time_point{};
    auto lap = [&](double& outMs) {
        if (!timing) return;
        const Clock::time_point now = Clock::now();
        outMs += std::chrono::duration<double, std::milli>(now - mark).count();
        mark = now;
    };

    // 1. Sources (addSources + clearSolidScalars).
    if (mine) {
        const SlabRange touched = addSourcesSlab(st, params, dt, slab);
        if (touched.k0 < touched.k1) {
            setBoundarySlab(st.density, BoundaryKind::Scalar, touched);
            setBoundarySlab(st.temperature, BoundaryKind::Scalar, touched);
        }
    }
    team.sync();
    if (obstacles) serialPart([&] { clearSolidScalars(st, params.ambientTemp, *obstacles); });
    lap(timings[Stage::Sources]);

    // 2. Forces: buoyancy, fans (D22), setBoundary, vorticity confinement (two passes), obstacles.
    // Without fans this is exactly the path from before D22. The fans read and write only their own cell, so
    // every thread can run them on its own slab right after the buoyancy, without a barrier in between.
    const bool fans = anyFanActive(params);
    if (mine) {
        applyBuoyancySlab(st, params, dt, slab);
        if (fans) {
            applyFansSlab(st, params, dt, slab);
            setBoundarySlab(st.u, BoundaryKind::VelocityX, slab);
            setBoundarySlab(st.w, BoundaryKind::VelocityZ, slab);
        }
        setBoundarySlab(st.v, BoundaryKind::VelocityY, slab);
    }
    team.sync();
    // With vorticity confinement the new velocity is written into u0, v0, w0 (confinementPassSlab), which is
    // exactly where the velocity advection wants its input after its swap; so the swap is skipped then.
    bool velocityInScratch = false;
    auto swapVelocity = [&] {
        st.u.swap(st.u0);
        st.v.swap(st.v0);
        st.w.swap(st.w0);
    };
    if (params.vorticityEps > 0.0f) {
        if (mine) {
            vorticityPassSlab(st, slab);
            setBoundarySlab(st.vorticityMagnitude, BoundaryKind::Scalar, slab);
        }
        team.sync();
        if (mine) {
            confinementPassSlab(st, params.vorticityEps * dt, slab);
            setBoundarySlab(st.u0, BoundaryKind::VelocityX, slab);
            setBoundarySlab(st.v0, BoundaryKind::VelocityY, slab);
            setBoundarySlab(st.w0, BoundaryKind::VelocityZ, slab);
        }
        team.sync();
        velocityInScratch = true;
    }
    if (obstacles)
        serialPart([&] {
            if (velocityInScratch) swapVelocity();  // the obstacle passes work on st.u, st.v, st.w
            applyPorousDrag(st, dt, *obstacles);
            enforceSolidVelocity(st, *obstacles);
            if (velocityInScratch) swapVelocity();
        });
    lap(timings[Stage::Forces]);

    // 3. Advect velocity (all three components share one back-trace) from u0, v0, w0 into u, v, w.
    if (!velocityInScratch) serialPart(swapVelocity);
    if (mine) {
        Field3D* const       dst[3] = {&st.u, &st.v, &st.w};
        const Field3D* const src[3] = {&st.u0, &st.v0, &st.w0};
        advectFields(dst, src, 3, st.u0, st.v0, st.w0, dt, slab);
        setVelocityBoundarySlab(st, slab);
    }
    team.sync();
    if (obstacles) serialPart([&] { enforceSolidVelocity(st, *obstacles); });
    lap(timings[Stage::AdvectVelocity]);

    // 4. Project: divergence, Jacobi (wavefront), subtract the gradient.
    if (mine) {
        computeDivergence(st.u, st.v, st.w, st.divergence, slab);
        setBoundarySlab(st.divergence, BoundaryKind::Scalar, slab);
    }
    team.sync();
    {
        const Clock::time_point jacobiStart = timing ? Clock::now() : Clock::time_point{};
        // D20: the solver and the starting value are parameters; the default (Jacobi from p = 0) is the reference.
        if (params.pressureSolver == PressureSolver::RedBlackGaussSeidel)
            redBlackSolve(st.pressure, st.divergence, params.jacobiIterations, params.sorOmega,
                          params.warmStartPressure, obstacles, team);
        else
            jacobiWavefront(st.pressure, st.pressure0, st.divergence, params.jacobiIterations, waveDepth, obstacles,
                            team, planesPerTick, params.warmStartPressure);
        if (timing && params.jacobiIterations > 0)
            timings.jacobiIterationMs =
                std::chrono::duration<double, std::milli>(Clock::now() - jacobiStart).count() /
                params.jacobiIterations;
    }
    lap(timings[Stage::Project]);

    // 4b + 5. Subtract the pressure gradient and advect the scalars in one pass (correctVelocityAndAdvectScalars:
    // the advection reads the velocity only at its own cell). Its time is counted as "advect scalars".
    // With obstacles the reference order is: gradient, reflectVelocityIntoSolids, setVelocityBoundary,
    // extendIntoSolids(density, temperature), swap, advect, setBoundary, clearSolidScalars, applyPorousFilter.
    // Here the reflection and the velocity ghost layer come after the advection instead. That gives the same
    // bits: the advection of a cell reads the velocity of that cell only, reflectVelocityIntoSolids changes
    // the velocity of solid cells only, and the scalars of solid cells are overwritten by clearSolidScalars
    // afterwards anyway (checked by test_parallel_solver.cpp, with obstacles).
    serialPart([&] {
        if (obstacles) {
            extendIntoSolids(st.density, *obstacles);
            extendIntoSolids(st.temperature, *obstacles);
        }
        st.density.swap(st.density0);
        st.temperature.swap(st.temperature0);
    });
    if (mine) {
        correctVelocityAndAdvectScalars(st, dt, slab);
        if (!obstacles) setVelocityBoundarySlab(st, slab);
        setBoundarySlab(st.density, BoundaryKind::Scalar, slab);
        setBoundarySlab(st.temperature, BoundaryKind::Scalar, slab);
    }
    team.sync();
    if (obstacles)
        serialPart([&] {
            reflectVelocityIntoSolids(st.u, st.v, st.w, *obstacles);
            setVelocityBoundary(st);
            clearSolidScalars(st, params.ambientTemp, *obstacles);
            applyPorousFilter(st, dt, *obstacles);
        });
    lap(timings[Stage::AdvectScalars]);
}

void poolBarrier(void* pool) { static_cast<ThreadPool*>(pool)->barrier(); }

} // namespace

void SmokeSolver::step(float dt) {
    Impl& s = *impl_;
    s.timings = StepTimings{};  // timings of the last step only

    ThreadPool* const pool = s.pool.get();
    const int threads = pool ? pool->numThreads() : 1;
    auto body = [&](int tid) {
        const serial::Team team{tid, threads, &poolBarrier, pool, s.config.threadWait == ThreadWait::Block};
        runStep(s.state, s.config.params, s.obstacles.get(), s.config.jacobiWaveDepth, s.config.jacobiPlanesPerTick,
                dt, team, s.timings);
    };
    if (pool && threads > 1)
        pool->parallelRun(body);
    else
        body(0);

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
