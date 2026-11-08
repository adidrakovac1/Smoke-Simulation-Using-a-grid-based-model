#include "smoke/SmokeSolver.h"

#include "Profiler.h"
#include "SmokeState.h"
#include "stages/Stages.h"

#include <utility>

namespace smoke {

struct SmokeSolver::Impl {
    SolverConfig config;
    SmokeState   state;
    StepTimings  timings;
    Diagnostics  diagnostics;
    long long    steps = 0;

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
    { ScopedTimer t(s.timings[Stage::Sources]);        addSources(s.state, s.config.params, all); }
    { ScopedTimer t(s.timings[Stage::Forces]);         applyForces(s.state, s.config.params, dt, all); }
    { ScopedTimer t(s.timings[Stage::AdvectVelocity]); advectVelocity(s.state, dt, all); }
    { ScopedTimer t(s.timings[Stage::Project]);        project(s.state, s.config.params, all); }
    { ScopedTimer t(s.timings[Stage::AdvectScalars]);  advectScalars(s.state, dt, all); }

    ++s.steps;
    // TODO(M2): fill s.diagnostics (max |div| before/after, mass, max speed, CFL, NaN count).
}

void SmokeSolver::reset() {
    impl_->state.clear();
    impl_->timings     = StepTimings{};
    impl_->diagnostics = Diagnostics{};
    impl_->steps       = 0;
}

void SmokeSolver::resize(const GridSize& newSize) {
    impl_->config.grid = newSize;
    impl_->state.resize(newSize);
    impl_->timings     = StepTimings{};
    impl_->diagnostics = Diagnostics{};
    impl_->steps       = 0;
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

const Diagnostics& SmokeSolver::diagnostics() const { return impl_->diagnostics; }
const StepTimings& SmokeSolver::lastTimings() const { return impl_->timings; }

} // namespace smoke
