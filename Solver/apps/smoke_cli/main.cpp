// smoke_cli: console program that runs the smoke simulation without any window or renderer.
// Output layout: docs/plan/CLI_OUTPUT.md. Text is ASCII only (Windows console code pages).

#include "smoke/SmokeSolver.h"
#include "selftest.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>
#include <chrono>

#ifndef SMOKE_BUILD_TYPE
#define SMOKE_BUILD_TYPE "unknown"
#endif

namespace {

struct Options {
    smoke::GridSize grid;      // default 64^3
    int  steps   = 100;
    int  warmup  = 10;
    bool quiet   = false;
    bool selftest = false;     // run the correctness tests instead of the simulation
};

bool parseInt(const char* text, int& out) {
    char* end = nullptr;
    const long v = std::strtol(text, &end, 10);
    if (end == text || *end != '\0' || v <= 0 || v > 4096) return false;
    out = static_cast<int>(v);
    return true;
}

void printUsage() {
    std::cout << "Koriscenje: smoke_cli [--grid N] [--nx N --ny N --nz N] [--steps N] [--warmup N] [--quiet]\n"
              << "            smoke_cli --selftest   (pokrece iste testove tacnosti kao smoke_tests)\n";
}

bool parseArgs(int argc, char** argv, Options& o) {
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](int& dst) { return i + 1 < argc && parseInt(argv[++i], dst); };
        if (a == "--grid") {
            int n = 0;
            if (!next(n)) return false;
            o.grid = {n, n, n};
        } else if (a == "--nx") { if (!next(o.grid.nx)) return false; }
        else if (a == "--ny")   { if (!next(o.grid.ny)) return false; }
        else if (a == "--nz")   { if (!next(o.grid.nz)) return false; }
        else if (a == "--steps")  { if (!next(o.steps)) return false; }
        else if (a == "--warmup") { if (!next(o.warmup)) return false; }
        else if (a == "--quiet")  { o.quiet = true; }
        else if (a == "--selftest") { o.selftest = true; }
        else if (a == "--help" || a == "-h") { printUsage(); std::exit(0); }
        else return false;
    }
    return true;
}

double median(std::vector<double> v) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    const std::size_t m = v.size() / 2;
    return (v.size() % 2) ? v[m] : 0.5 * (v[m - 1] + v[m]);
}

} // namespace

int main(int argc, char** argv) {
    Options opt;
    if (!parseArgs(argc, argv, opt)) { printUsage(); return 2; }

    // Handled before the solver is constructed, so the normal run path is unchanged.
    if (opt.selftest) {
        std::cout << "=== Smoke Solver: samoprovjera (--selftest) ===\n"
                  << "Pokrecemo iste testove tacnosti kao smoke_tests.\n\n";
        return smoke_test::runAll(std::cout);
    }

    smoke::SolverConfig cfg;
    cfg.grid = opt.grid;
    smoke::SmokeSolver solver(cfg);

    std::cout << "=== Smoke Solver (serijska verzija) ===\n"
              << "Pocinjemo simulaciju dima.\n\n"
              << "Gradnja:  " << SMOKE_BUILD_TYPE << "\n"
              << "Racunar:  n/a (TODO M3: --calibrate)\n"
              << "Konfig:   mreza " << cfg.grid.nx << "x" << cfg.grid.ny << "x" << cfg.grid.nz
              << ", koraka " << opt.steps << " (+ " << opt.warmup << " zagrijavanja)"
              << ", dt " << cfg.params.dt << ", Jacobi iteracija " << cfg.params.jacobiIterations << "\n"
              << "NAPOMENA: ovo je skelet. Faze simulacije jos nisu implementirane (vidi docs/plan/PLAN.md).\n\n";

    for (int i = 0; i < opt.warmup; ++i) solver.step();

    std::vector<std::vector<double>> perStage(smoke::kStageCount);
    std::vector<double> perStep;

    using Clock = std::chrono::steady_clock;
    const auto t0 = Clock::now();
    for (int i = 1; i <= opt.steps; ++i) {
        solver.step();
        const smoke::StepTimings& t = solver.lastTimings();
        for (std::size_t s = 0; s < smoke::kStageCount; ++s) perStage[s].push_back(t.ms[s]);
        perStep.push_back(t.total());
        // Printing is done between steps and is NOT part of the timed code.
        if (!opt.quiet && (i % std::max(1, opt.steps / 10) == 0))
            std::cout << "  [" << std::setw(4) << i << "/" << opt.steps << "]  "
                      << std::fixed << std::setprecision(4) << t.total() << " ms/korak\n";
    }
    const double wallSeconds = std::chrono::duration<double>(Clock::now() - t0).count();

    std::cout << "\nZavrseno. Ukupno vrijeme: " << std::fixed << std::setprecision(3) << wallSeconds
              << " s (" << opt.steps << " koraka)\n\n"
              << "Vrijeme po fazi (medijan po koraku):\n"
              << "  " << std::left << std::setw(26) << "Faza" << std::right << std::setw(12) << "ms" << std::setw(10) << "udio\n";

    const double medTotal = median(perStep);
    for (std::size_t s = 0; s < smoke::kStageCount; ++s) {
        const double m = median(perStage[s]);
        std::cout << "  " << std::left << std::setw(26) << smoke::stageName(static_cast<smoke::Stage>(s))
                  << std::right << std::setw(12) << std::setprecision(4) << m
                  << std::setw(8) << std::setprecision(1) << (medTotal > 0 ? 100.0 * m / medTotal : 0.0) << "%\n";
    }
    std::cout << "  " << std::left << std::setw(26) << "UKUPNO po koraku" << std::right << std::setw(12)
              << std::setprecision(4) << medTotal << "\n\n";

    const smoke::Diagnostics& d = solver.diagnostics();
    std::cout << "Dijagnostika: " << (d.computed ? "" : "jos nije implementirana (TODO M2)") << "\n";
    if (d.computed) {
        std::cout << "  max|div| prije/poslije projekcije: " << d.maxDivBefore << " / " << d.maxDivAfter << "\n"
                  << "  ukupna masa dima:                  " << d.totalMass << "\n"
                  << "  max|u|, CFL:                       " << d.maxSpeed << ", " << d.cfl << "\n"
                  << "  NaN/Inf celija:                    " << d.nonFiniteCells << "\n";
    }

    const bool ok = (d.nonFiniteCells == 0);
    std::cout << "Status: " << (ok ? "OK" : "GRESKA (NaN/Inf)") << "\n";
    return ok ? 0 : 1;
}
