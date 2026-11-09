// smoke_cli: console program that runs the smoke simulation without any window or renderer.
// Output layout: docs/plan/CLI_OUTPUT.md. Text is ASCII only (Windows console code pages).

#include "smoke/SmokeSolver.h"
#include "PgmWriter.h"
#include "selftest.h"

#include <algorithm>
#include <sstream>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cerrno>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif

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
    std::string demo;          // "advect": the M1 demo (tests/demo_advect.cpp)
    std::string dumpFolder;    // if set, save x-y density slices as PGM (docs/plan/CLI_OUTPUT.md)
    int  every   = 10;         // save a slice every N steps
    float dt     = 0.0f;       // > 0: override SmokeParams::dt
    float vorticityEps = -1.0f;
    int  jacobi  = 0;          // > 0: override SmokeParams::jacobiIterations
};

bool parseInt(const char* text, int& out, int minValue = 1) {
    char* end = nullptr;
    const long v = std::strtol(text, &end, 10);
    if (end == text || *end != '\0' || v < minValue || v > 4096) return false;
    out = static_cast<int>(v);
    return true;
}

bool parseFloat(const char* text, float& out) {
    char* end = nullptr;
    const float v = std::strtof(text, &end);
    if (end == text || *end != '\0' || !(v > 0.0f) || v > 1.0f) return false;  // dt in (0, 1] s
    out = v;
    return true;
}

bool parseNonnegativeFloat(const char* text, float& out) {
    char* end = nullptr;
    const float v = std::strtof(text, &end);
    if (end == text || *end != '\0' || !std::isfinite(v) || v < 0.0f || v > 10.0f) return false;
    out = v;
    return true;
}

void printUsage() {
    std::cout << "Koriscenje: smoke_cli [--grid N] [--nx N --ny N --nz N] [--steps N] [--warmup N] [--quiet]\n"
              << "                      [--dt X] [--jacobi K] [--vorticity-eps X] [--dump-slices FOLDER] [--every N]\n"
              << "            smoke_cli --selftest   (pokrece iste testove tacnosti kao smoke_tests)\n"
              << "            smoke_cli --demo advect [--dump-slices FOLDER] [--every N]\n"
              << "                                    (M1: kugla gustoce nosena konstantnom brzinom)\n";
}

bool parseArgs(int argc, char** argv, Options& o) {
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](int& dst) { return i + 1 < argc && parseInt(argv[++i], dst); };
        auto nextText = [&](std::string& dst) {
            if (i + 1 >= argc) return false;
            dst = argv[++i];
            return !dst.empty();
        };
        if (a == "--grid") {
            int n = 0;
            if (!next(n)) return false;
            o.grid = {n, n, n};
        } else if (a == "--nx") { if (!next(o.grid.nx)) return false; }
        else if (a == "--ny")   { if (!next(o.grid.ny)) return false; }
        else if (a == "--nz")   { if (!next(o.grid.nz)) return false; }
        else if (a == "--steps")  { if (!next(o.steps)) return false; }
        else if (a == "--warmup") { if (i + 1 >= argc || !parseInt(argv[++i], o.warmup, 0)) return false; }
        else if (a == "--quiet")  { o.quiet = true; }
        else if (a == "--selftest") { o.selftest = true; }
        else if (a == "--demo")        { if (!nextText(o.demo) || o.demo != "advect") return false; }
        else if (a == "--dump-slices") { if (!nextText(o.dumpFolder)) return false; }
        else if (a == "--every")       { if (!next(o.every)) return false; }
        else if (a == "--dt")          { if (i + 1 >= argc || !parseFloat(argv[++i], o.dt)) return false; }
        else if (a == "--jacobi")      { if (!next(o.jacobi)) return false; }
        else if (a == "--vorticity-eps") {
            if (i + 1 >= argc || !parseNonnegativeFloat(argv[++i], o.vorticityEps)) return false;
        }
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

bool createDirectories(const std::string& path) {
    auto makeDirectory = [](const std::string& directory) {
#ifdef _WIN32
    if (_mkdir(directory.c_str()) == 0) return true;
    struct _stat info;
    return errno == EEXIST && _stat(directory.c_str(), &info) == 0 && (info.st_mode & _S_IFDIR);
#else
    if (mkdir(directory.c_str(), 0777) == 0) return true;
    struct stat info;
    return errno == EEXIST && stat(directory.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
#endif
    };

    std::string current;
    for (char c : path) {
    current += c;
#ifdef _WIN32
    const bool separator = c == '/' || c == '\\';
#else
    const bool separator = c == '/';
#endif
    if (separator && current.size() > 1 && !makeDirectory(current.substr(0, current.size() - 1)))
        return false;
    }
    return !path.empty() && makeDirectory(path);
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
    if (!opt.demo.empty()) return smoke_test::runAdvectionDemo(opt.dumpFolder, opt.every, std::cout);

    smoke::SolverConfig cfg;
    cfg.grid = opt.grid;
    if (opt.dt > 0.0f) cfg.params.dt = opt.dt;
    if (opt.jacobi > 0) cfg.params.jacobiIterations = opt.jacobi;
    if (opt.vorticityEps >= 0.0f) cfg.params.vorticityEps = opt.vorticityEps;
    smoke::SmokeSolver solver(cfg);

    std::cout << "=== Smoke Solver (serijska verzija) ===\n"
              << "Pocinjemo simulaciju dima.\n\n"
              << "Gradnja:  " << SMOKE_BUILD_TYPE << "\n"
    << "Racunar:  otisak: smoke_bench --calibrate\n"
              << "Konfig:   mreza " << cfg.grid.nx << "x" << cfg.grid.ny << "x" << cfg.grid.nz
              << ", koraka " << opt.steps << " (+ " << opt.warmup << " zagrijavanja)"
              << ", dt " << cfg.params.dt << ", Jacobi iteracija " << cfg.params.jacobiIterations << "\n"
              << "Izvor:    pozicija (" << cfg.params.source.x << ", " << cfg.params.source.y << ", "
              << cfg.params.source.z << "), r = " << cfg.params.source.radius << "*nx, gustoca "
              << cfg.params.source.densityRate << "/s, toplota " << cfg.params.source.temperatureRate << "/s\n"
              << "Uzgon:    alpha " << cfg.params.buoyancyAlpha << ", beta " << cfg.params.buoyancyBeta
              << ", T_amb " << cfg.params.ambientTemp << ", vorticity epsilon " << cfg.params.vorticityEps << "\n"
              << "Faze:     izvor, sile, advekcija brzine, projekcija (Jacobi), advekcija gustoce;\n"
              << "          vorticity confinement je ukljucen kad je epsilon > 0.\n\n";

    // Diagnostics cost several passes over the grid, so they are computed only when printed, between
    // steps, outside the per-stage timers (docs/plan/IMPLEMENTATION_PLAN.md, P3).
    auto printDiagnosticsShort = [&](const smoke::Diagnostics& d) {
        std::cout << std::scientific << std::setprecision(2) << "   max|div| " << d.maxDivBefore << " -> "
                  << d.maxDivAfter << "   masa " << d.totalMass << "   max|u| " << d.maxSpeed
                  << (d.nonFiniteCells ? "   NaN/Inf!" : "") << std::defaultfloat << "\n";
    };

    // Slices are written between steps, so file output is never part of the measured step time.
    const bool dump = !opt.dumpFolder.empty();
    if (dump && !createDirectories(opt.dumpFolder)) {
        std::cout << "  GRESKA: ne mogu kreirati direktorij " << opt.dumpFolder << "\n";
        return 1;
    }
    auto saveSlice = [&](int step) {
        const smoke::DensityView v = solver.density();
        const int k = v.nz / 2;
        const float peak = smoke_io::maxInSliceXY(v, k);  // auto grey scale: brightest pixel = slice maximum
        std::ostringstream name;
        name << opt.dumpFolder << "/slice_" << std::setw(5) << std::setfill('0') << step << ".pgm";
        if (!smoke_io::writePgmSliceXY(name.str(), v, k, peak))
            std::cout << "  GRESKA: ne mogu pisati " << name.str() << "\n";
    };

    for (int i = 0; i < opt.warmup; ++i) solver.step();

    std::vector<std::vector<double>> perStage(smoke::kStageCount);
    std::vector<double> perJacobiIteration;
    std::vector<double> perStep;

    using Clock = std::chrono::steady_clock;
    const auto t0 = Clock::now();
    for (int i = 1; i <= opt.steps; ++i) {
        solver.step();
        const smoke::StepTimings& t = solver.lastTimings();
        for (std::size_t s = 0; s < smoke::kStageCount; ++s) perStage[s].push_back(t.ms[s]);
        perJacobiIteration.push_back(t.jacobiIterationMs);
        perStep.push_back(t.total());
        if (dump && i % std::max(1, opt.every) == 0) saveSlice(i);
        // Printing is done between steps and is NOT part of the timed code.
        if (!opt.quiet && (i % std::max(1, opt.steps / 10) == 0)) {
            std::cout << "  [" << std::setw(4) << i << "/" << opt.steps << "]  " << std::fixed
                      << std::setprecision(3) << std::setw(8) << t.total() << " ms/korak";
            printDiagnosticsShort(solver.diagnostics());
        }
    }
    const double wallSeconds = std::chrono::duration<double>(Clock::now() - t0).count();

    std::cout << "\nZavrseno. Ukupno vrijeme (zidni sat, ukljucuje ispis): " << std::fixed << std::setprecision(3)
              << wallSeconds << " s (" << opt.steps << " koraka)\n\n"
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
    std::cout << "Jacobi po iteraciji (unutar projekcije): " << std::setprecision(4)
              << median(perJacobiIteration) << " ms\n\n";

    const smoke::Diagnostics d = solver.diagnostics();
    std::cout << "Dijagnostika (stanje poslije zadnjeg koraka):\n" << std::scientific << std::setprecision(3)
              << "  max|div| prije/poslije projekcije: " << d.maxDivBefore << " / " << d.maxDivAfter << "\n"
              << "  ukupna masa dima:                  " << d.totalMass << "\n"
              << "  max|u|, CFL:                       " << d.maxSpeed << ", " << d.cfl << "\n"
              << std::defaultfloat
              << "  NaN/Inf celija:                    " << d.nonFiniteCells << "\n";

    const bool ok = (d.nonFiniteCells == 0);
    std::cout << "Status: " << (ok ? "OK" : "GRESKA (NaN/Inf)") << "\n";
    return ok ? 0 : 1;
}
