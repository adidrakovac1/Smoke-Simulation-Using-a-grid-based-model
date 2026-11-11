// smoke_prof: a steady workload for a sampling profiler (AMD uProf). docs/plan/EXECUTION_PLAN.md, phase P1.
//
// A sampling profiler interrupts the program many times per second and notes which instruction was running.
// To get enough samples in the kernels we care about, this program develops a realistic flow first and then
// repeats ONE kind of work for a fixed number of seconds, without printing anything in between.
//
//   --stage all      whole simulation steps, exactly the order of SmokeSolver::step
//   --stage jacobi   pressure solve only: p = 0, then K Jacobi iterations (each followed by setBoundary)
//   --stage advect   both advections of one step (3 velocity fields, then density + temperature)
//   --stage forces   buoyancy + vorticity confinement
//
// It uses the internal stage functions directly (like the tests and smoke_lab), so it measures the same code
// the solver runs. Text is ASCII only (decision D7).

#include "SmokeState.h"
#include "smoke/SolverConfig.h"
#include "stages/Stages.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
using namespace smoke;
using namespace smoke::serial;

struct Options {
    int grid = 64;
    double seconds = 10.0;
    std::string stage = "all";
    int develop = 100;     // whole steps run before the measured part (the reference scenario also runs 100)
    int resetEvery = 300;  // --stage all: start again from an empty box after this many steps (see runAll)
};

void printUsage() {
    std::cout << "Koriscenje: smoke_prof [--grid N] [--seconds S] [--stage all|jacobi|advect|forces]\n"
                 "                       [--develop N] [--reset-every N]\n"
                 "  --grid N         kubna mreza N^3 (zadano 64)\n"
                 "  --seconds S      koliko dugo se ponavlja izabrani posao (zadano 10)\n"
                 "  --stage X        all = cijeli korak; jacobi, advect, forces = samo ta faza (zadano all)\n"
                 "  --develop N      cijelih koraka prije mjerenog dijela, da polja budu stvarna (zadano 100)\n"
                 "  --reset-every N  samo za 'all': poslije N koraka kreni od prazne kutije (zadano 300)\n";
}

bool parseInt(const char* text, int& out, int minValue) {
    char* end = nullptr;
    const long value = std::strtol(text, &end, 10);
    if (end == text || *end != '\0' || value < minValue || value > 1000000) return false;
    out = static_cast<int>(value);
    return true;
}

bool parseArgs(int argc, char** argv, Options& options) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            printUsage();
            std::exit(0);
        }
        if (i + 1 >= argc) return false;
        const char* value = argv[++i];
        if (arg == "--grid") {
            if (!parseInt(value, options.grid, 4) || options.grid > 512) return false;
        } else if (arg == "--seconds") {
            char* end = nullptr;
            options.seconds = std::strtod(value, &end);
            if (end == value || *end != '\0' || !(options.seconds > 0.0) || options.seconds > 3600.0) return false;
        } else if (arg == "--stage") {
            options.stage = value;
            if (options.stage != "all" && options.stage != "jacobi" && options.stage != "advect" &&
                options.stage != "forces")
                return false;
        } else if (arg == "--develop") {
            if (!parseInt(value, options.develop, 0)) return false;
        } else if (arg == "--reset-every") {
            if (!parseInt(value, options.resetEvery, 1)) return false;
        } else {
            return false;
        }
    }
    return true;
}

// One whole step, the same calls in the same order as SmokeSolver::step (Solver/src/SmokeSolver.cpp).
void wholeStep(SmokeState& state, const SmokeParams& params, SlabRange all) {
    addSources(state, params, params.dt, all);
    applyForces(state, params, params.dt, all);
    advectVelocity(state, params.dt, all);
    project(state, params, all);
    advectScalars(state, params.dt, all);
}

double secondsSince(Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

// Whole steps. The box is closed and the source never stops, so after many steps the smoke fills the box and
// the flow no longer looks like the scenario we tuned and tested (300 steps: the plume reaches the top,
// docs/plan/MATH.md, section 3). Therefore the simulation restarts from an empty box every resetEvery steps.
long long runAll(SmokeState& state, const SmokeParams& params, SlabRange all, const Options& options) {
    long long iterations = 0;
    int stepsSinceReset = options.develop;
    const auto start = Clock::now();
    while (secondsSince(start) < options.seconds) {
        if (stepsSinceReset >= options.resetEvery) {
            state.clear();
            stepsSinceReset = 0;
        }
        wholeStep(state, params, all);
        ++stepsSinceReset;
        ++iterations;
    }
    return iterations;
}

// The pressure solve of project(): p = 0, then K iterations, each followed by setBoundary. The divergence
// is computed once from the developed velocity and stays fixed. One "iteration" here = one Jacobi iteration.
long long runJacobi(SmokeState& state, const SmokeParams& params, SlabRange all, const Options& options) {
    computeDivergence(state.u, state.v, state.w, state.divergence, all);
    setBoundary(state.divergence, BoundaryKind::Scalar);
    long long iterations = 0;
    const auto start = Clock::now();
    while (secondsSince(start) < options.seconds) {
        state.pressure.fill(0.0f);
        for (int it = 0; it < params.jacobiIterations; ++it) {
            state.pressure.swap(state.pressure0);
            jacobiIteration(state.pressure, state.pressure0, state.divergence, all);
            setBoundary(state.pressure, BoundaryKind::Scalar);
        }
        iterations += params.jacobiIterations;
    }
    return iterations;
}

// The two advections of one step, always from the same developed fields into the spare (*0) fields, so the
// input never changes: repeated advection without projection would smooth the velocity away and the memory
// access pattern (which depends on the velocity) would stop being realistic.
// One "iteration" = advection of u, v, w plus advection of density and temperature. No setBoundary.
long long runAdvect(SmokeState& state, const SmokeParams& params, SlabRange all, const Options& options) {
    Field3D* const velocityDst[3] = {&state.u0, &state.v0, &state.w0};
    const Field3D* const velocitySrc[3] = {&state.u, &state.v, &state.w};
    Field3D* const scalarDst[2] = {&state.density0, &state.temperature0};
    const Field3D* const scalarSrc[2] = {&state.density, &state.temperature};
    long long iterations = 0;
    const auto start = Clock::now();
    while (secondsSince(start) < options.seconds) {
        advectFields(velocityDst, velocitySrc, 3, state.u, state.v, state.w, params.dt, all);
        advectFields(scalarDst, scalarSrc, 2, state.u, state.v, state.w, params.dt, all);
        ++iterations;
    }
    return iterations;
}

// Buoyancy + vorticity confinement. applyForces adds to the velocity, so repeating it would make the velocity
// grow without limit. The developed velocity is therefore restored (memcpy) before every call; the copy shows
// up in the profile under memcpy, separately from the force kernels.
long long runForces(SmokeState& state, const SmokeParams& params, SlabRange all, const Options& options) {
    const std::size_t count = state.u.totalFloats();
    const std::vector<float> savedU(state.u.data(), state.u.data() + count);
    const std::vector<float> savedV(state.v.data(), state.v.data() + count);
    const std::vector<float> savedW(state.w.data(), state.w.data() + count);
    long long iterations = 0;
    const auto start = Clock::now();
    while (secondsSince(start) < options.seconds) {
        std::memcpy(state.u.data(), savedU.data(), count * sizeof(float));
        std::memcpy(state.v.data(), savedV.data(), count * sizeof(float));
        std::memcpy(state.w.data(), savedW.data(), count * sizeof(float));
        applyForces(state, params, params.dt, all);
        ++iterations;
    }
    return iterations;
}

} // namespace

int main(int argc, char** argv) {
    Options options;
    if (!parseArgs(argc, argv, options)) {
        printUsage();
        return 2;
    }

    SolverConfig config;  // default parameters: the same scenario as smoke_bench and the reference result
    config.grid = {options.grid, options.grid, options.grid};
    const SmokeParams& params = config.params;
    SmokeState state;
    state.resize(config.grid);
    const SlabRange all = wholeInterior(config.grid);

    for (int step = 0; step < options.develop; ++step) wholeStep(state, params, all);

    const auto start = Clock::now();
    long long iterations = 0;
    if (options.stage == "all") iterations = runAll(state, params, all, options);
    else if (options.stage == "jacobi") iterations = runJacobi(state, params, all, options);
    else if (options.stage == "advect") iterations = runAdvect(state, params, all, options);
    else iterations = runForces(state, params, all, options);
    const double seconds = secondsSince(start);

    // Reading a few cells keeps the compiler from treating the work as unused, and shows the state is finite.
    const int mid = options.grid / 2;
    const float checksum = state.density.at(mid, mid, mid) + state.u.at(mid, mid, mid) + state.v.at(mid, mid, mid) +
                           state.pressure.at(mid, mid, mid) + state.u0.at(mid, mid, mid);

    std::cout << "smoke_prof: faza=" << options.stage << " mreza=" << options.grid << "^3"
              << " razvoj_koraka=" << options.develop << " K=" << params.jacobiIterations << '\n'
              << std::fixed << std::setprecision(3) << "  trajanje_s: " << seconds << '\n'
              << "  ponavljanja: " << iterations << '\n'
              << std::setprecision(6) << "  ms_po_ponavljanju: " << (iterations ? seconds * 1000.0 / iterations : 0.0)
              << '\n'
              << "  kontrolna_vrijednost: " << checksum << '\n';
    return checksum == checksum ? 0 : 1;  // NaN is the only value not equal to itself
}
