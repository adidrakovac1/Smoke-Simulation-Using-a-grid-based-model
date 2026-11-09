// smoke_demo: presentation program for the course (Paralelni racunarski sistemi). It runs the WHOLE smoke
// simulation in three versions in the same program, with the same initial state and parameters:
//   A. "Neoptimizirana verzija": the M3 code (commit 73ceb9e), compiled without vectorization (like B0);
//   B. the same M3 code with the default Release flags, i.e. with the compiler's automatic vectorization (B1);
//   C. "Optimizirana verzija": the current solver after M4 (smoke::SmokeSolver).
// It prints total time, time per step, steps per second, time per phase, speedup S and a bit-by-bit check of
// the final density field. Part 2 compares the individual techniques per kernel (LabRunner, as smoke_lab),
// including SoA vs AoS and manual loop unrolling. Part 3 lists which functions were changed and how.
//
// Measurement: warm-up steps, then N measured steps; 3 interleaved rounds with the version order rotated each
// round (decision D10); medians over rounds. Output is ASCII Bosnian (decision D7).

#include "CpuFeatures.h"
#include "DemoM3.h"
#include "LabRunner.h"
#include "SmokeState.h"

#include "smoke/SmokeSolver.h"
#include "smoke/StepTimings.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#ifndef SMOKE_DEMO_COMMIT
#define SMOKE_DEMO_COMMIT "unknown"
#endif
#ifndef SMOKE_DEMO_FLAGS
#define SMOKE_DEMO_FLAGS "unknown"
#endif

namespace {

using smoke::GridSize;
using smoke::Stage;
using smoke::StepTimings;

struct Options {
    std::vector<int> grids{64};
    int steps = 100;
    int warmup = 10;
    int rounds = 3;
    bool full = false;
    bool kernels = true;
};

constexpr int kVersions = 3;
const char* const kVersionNames[kVersions] = {
    "A. Neoptimizirana verzija (M3, bez vekt.)",
    "B. M3 + auto-vektorizacija kompajlera",
    "C. Optimizirana verzija (M4)",
};
const char* const kVersionShort[kVersions] = {"Neopt. (A)", "M3+auto (B)", "Opt. (C)"};

double medianOf(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    const std::size_t m = v.size() / 2;
    return (v.size() % 2) ? v[m] : 0.5 * (v[m - 1] + v[m]);
}

// Result of one run of one version: measured time (sum over the N measured steps) per stage and in total,
// and the final density field (interior cells, packed) for the bit-by-bit comparison.
struct RunResult {
    std::array<double, smoke::kStageCount> stageMs{};
    double totalMs = 0.0;
    std::vector<float> density;
};

void accumulate(RunResult& r, const StepTimings& t) {
    for (std::size_t s = 0; s < smoke::kStageCount; ++s) r.stageMs[s] += t.ms[s];
    r.totalMs += t.total();
}

std::vector<float> packInterior(const float* origin, int nx, int ny, int nz, std::ptrdiff_t sJ, std::ptrdiff_t sK) {
    std::vector<float> out;
    out.reserve(static_cast<std::size_t>(nx) * ny * nz);
    for (int k = 0; k < nz; ++k)
        for (int j = 0; j < ny; ++j)
            for (int i = 0; i < nx; ++i) out.push_back(origin[i + j * sJ + k * sK]);
    return out;
}

// Versions A and B: the M3 step (DemoM3.h) on our own SmokeState.
RunResult runM3(int version, const GridSize& g, const Options& o) {
    smoke::SmokeParams params;  // defaults, as SmokeSolver uses them
    smoke::SmokeState state;
    state.resize(g);
    StepTimings t;
    auto step = [&] {
        if (version == 0) smoke_demo::m3_b0::m3Step(state, params, t);
        else smoke_demo::m3_b1::m3Step(state, params, t);
    };
    for (int i = 0; i < o.warmup; ++i) step();
    RunResult r;
    for (int i = 0; i < o.steps; ++i) {
        step();
        accumulate(r, t);
    }
    const smoke::Field3D& d = state.density;
    r.density = packInterior(&d.at(1, 1, 1), g.nx, g.ny, g.nz, d.strideJ(), d.strideK());
    return r;
}

// Version C: the optimized solver through its public interface (the same one smoke_cli and the renderer use).
RunResult runOptimized(const GridSize& g, const Options& o) {
    smoke::SolverConfig config;
    config.grid = g;
    smoke::SmokeSolver solver(config);
    for (int i = 0; i < o.warmup; ++i) solver.step();
    RunResult r;
    for (int i = 0; i < o.steps; ++i) {
        solver.step();
        accumulate(r, solver.lastTimings());
    }
    const smoke::DensityView v = solver.density();
    r.density = packInterior(v.origin, v.nx, v.ny, v.nz, v.strideJ, v.strideK);
    return r;
}

RunResult runVersion(int version, const GridSize& g, const Options& o) {
    return version < 2 ? runM3(version, g, o) : runOptimized(g, o);
}

std::string accuracyText(const std::vector<float>& a, const std::vector<float>& ref) {
    if (a.size() != ref.size()) return "RAZLICITA velicina";
    long long diff = 0;
    for (std::size_t i = 0; i < a.size(); ++i) diff += std::memcmp(&a[i], &ref[i], sizeof(float)) != 0;
    return diff == 0 ? "bit po bit isto" : "RAZLIKA u " + std::to_string(diff) + " celija";
}

struct GridSummary {
    int n = 0;
    double speedupVsA = 0.0, speedupVsB = 0.0;
};

GridSummary simulateGrid(int n, const Options& o) {
    const GridSize g{n, n, n};
    std::cout << "\nMreza " << n << "^3 (" << static_cast<long long>(n) * n * n << " celija), " << o.steps
              << " izmjerenih koraka (+" << o.warmup << " zagrijavanje), broj rundi: " << o.rounds << "\n";

    // Interleaved rounds: in round r the order is rotated by r (A B C, B C A, C A B).
    std::array<std::vector<double>, kVersions> totals;
    std::array<std::array<std::vector<double>, smoke::kStageCount>, kVersions> stages;
    std::array<std::string, kVersions> accuracy;
    std::vector<float> reference;
    std::array<std::vector<float>, kVersions> finalDensity;
    for (int r = 0; r < o.rounds; ++r) {
        std::cout << "  runda " << r + 1 << "/" << o.rounds << " ..." << std::flush;
        for (int step = 0; step < kVersions; ++step) {
            const int v = (step + r) % kVersions;
            RunResult res = runVersion(v, g, o);
            totals[v].push_back(res.totalMs);
            for (std::size_t s = 0; s < smoke::kStageCount; ++s) stages[v][s].push_back(res.stageMs[s]);
            if (r == 0) finalDensity[v] = std::move(res.density);
        }
        std::cout << " gotovo\n";
    }
    for (int v = 0; v < kVersions; ++v)
        accuracy[v] = v == 0 ? "referenca" : accuracyText(finalDensity[v], finalDensity[0]);

    std::array<double, kVersions> total{};
    for (int v = 0; v < kVersions; ++v) total[v] = medianOf(totals[v]);

    std::cout << "\n  " << std::left << std::setw(44) << "verzija" << std::right << std::setw(10) << "ukupno s"
              << std::setw(10) << "ms/korak" << std::setw(10) << "koraka/s" << std::setw(11) << "S prema A"
              << std::setw(11) << "S prema B" << "   tacnost (gustina)\n";
    for (int v = 0; v < kVersions; ++v) {
        const double perStep = total[v] / o.steps;
        std::cout << "  " << std::left << std::setw(44) << kVersionNames[v] << std::right << std::fixed
                  << std::setprecision(3) << std::setw(10) << total[v] / 1000.0 << std::setw(10) << perStep
                  << std::setprecision(1) << std::setw(10) << 1000.0 / perStep << std::setprecision(2)
                  << std::setw(10) << total[0] / total[v] << "x" << std::setw(10) << total[1] / total[v] << "x"
                  << "   " << accuracy[v] << "\n"
                  << std::defaultfloat;
    }

    std::cout << "\n  Vrijeme po fazi (ms po koraku, medijan rundi):\n  " << std::left << std::setw(26) << "faza"
              << std::right;
    for (int v = 0; v < kVersions; ++v) std::cout << std::setw(13) << kVersionShort[v];
    std::cout << std::setw(16) << "S (C prema A)" << std::setw(16) << "S (C prema B)" << std::setw(12) << "udio u C"
              << "\n";
    for (std::size_t s = 0; s < smoke::kStageCount; ++s) {
        std::array<double, kVersions> ms{};
        for (int v = 0; v < kVersions; ++v) ms[v] = medianOf(stages[v][s]) / o.steps;
        std::cout << "  " << std::left << std::setw(26) << smoke::stageName(static_cast<Stage>(s)) << std::right
                  << std::fixed << std::setprecision(3);
        for (int v = 0; v < kVersions; ++v) std::cout << std::setw(13) << ms[v];
        std::cout << std::setprecision(2) << std::setw(15) << (ms[2] > 0 ? ms[0] / ms[2] : 0.0) << "x"
                  << std::setw(15) << (ms[2] > 0 ? ms[1] / ms[2] : 0.0) << "x" << std::setprecision(1)
                  << std::setw(10) << 100.0 * ms[2] * o.steps / total[2] << " %\n"
                  << std::defaultfloat;
    }
    std::cout << "  " << std::left << std::setw(26) << "UKUPNO (cijeli korak)" << std::right << std::fixed
              << std::setprecision(3);
    for (int v = 0; v < kVersions; ++v) std::cout << std::setw(13) << total[v] / o.steps;
    std::cout << std::setprecision(2) << std::setw(15) << total[0] / total[2] << "x" << std::setw(15)
              << total[1] / total[2] << "x" << std::setw(12) << "100.0 %" << "\n"
              << std::defaultfloat;
    return {n, total[0] / total[2], total[1] / total[2]};
}

void printWhatChanged() {
    std::cout <<
R"(
==========================================================================================================
 DIO 3: Sta je optimizirano (Neoptimizirana verzija A -> Optimizirana verzija C)
==========================================================================================================
Algoritam je ISTI u svim verzijama (Stam 1999/2003, Fedkiw et al. 2001), pa je i rezultat bit po bit isti:
  izvor dima -> uzgon + vorticity confinement -> semi-Lagrangeova advekcija brzine
  -> projekcija pritiska (divergencija, 40 Jacobi iteracija, oduzimanje gradijenta)
  -> advekcija gustine i temperature.
Mijenjan je samo NACIN na koji procesor racuna (serijski, jedna nit; bez -ffast-math, bez -march=native).

1. Projekcija pritiska: jacobiIteration, computeDivergence, subtractPressureGradient
   (src/stages/Projection.cpp, JacobiRows.inc, Projection_avx2.cpp)
   - restrict pokazivaci + unutrasnja petlja kao funkcija po redu ........ T12, M4-007
   - AVX2 (8 floatova odjednom) samo u jednom fajlu, uz provjeru procesora
     u toku rada i rezervnu SSE putanju za procesore bez AVX2 ............ T15/T16, M4-009
   - probano i NIJE zadrzano: #pragma omp simd (isti kod, M4-008), blokiranje
     ravnina (M4-004), rucne AVX2 intrinsics (nisu brze od kompajlera, M4-010)
2. Advekcija: advectScalar / advectFields, advectVelocity, advectScalars
   (src/stages/Advection.cpp, AdvectionRows.inc, Advection_avx2.cpp, Interpolation.h)
   - clamp bez grananja (min/max umjesto if) ............................ T10, M4-011
   - restrict + sirovi pokazivaci u interpolaciji ....................... T12, M4-012
   - 32-bitni indeks celije + funkcija reda van linije: tek tada kompajler
     vektorizira petlju (8 citanja = "gather") ............................. T7,  M4-013
   - AVX2 po fajlu s provjerom procesora ................................. T15/T16, M4-014
   - FUZIJA JEZGARA: u, v, w u jednom prolazu (jedan trag unazad za sva tri
     polja), gustina i temperatura u jednom prolazu ........................ nivo 2, M4-015/016
   - kompajler pusten na 8 floatova u spojenoj AVX2 petlji ................ M4-017
3. Sile: applyVorticityConfinement (src/stages/Forces.cpp, ForcesRows.inc, Forces_avx2.cpp)
   - maske bez grananja umjesto if (gradijent 0) .......................... T10, M4-003, M4-006
   - -fno-math-errno samo za taj fajl: sqrt bez poziva biblioteke ........ M4-018
   - restrict + funkcije po redu + maska umjesto ?: kod djelitelja:
     tek tada kompajler vektorizira obje petlje ............................ T12, M4-019
   - AVX2 po fajlu s provjerom procesora ................................. T15/T16, M4-020
4. Raspored memorije: SoA (poseban niz za svaku velicinu, red poravnat na 8 floatova / 32 bajta)
   je izabran od pocetka (M0), zato ga dio 2 poredi s AoS rasporedom (struct po celiji).
5. Odmotavanje petlji: u verziji C ga radi kompajler (-O3, "unroll factor 4/8" u izvjestaju
   -fopt-info-vec); dio 2 pokazuje i rucno odmotavanje (T3) na M3 kodu.
Detalji, hipoteze i sva mjerenja: docs/plan/PERF_LOG.md (zbir u odjeljku 7).
)";
}

bool parsePositive(const char* text, int& out) {
    char* end = nullptr;
    const long v = std::strtol(text, &end, 10);
    if (end == text || *end != '\0' || v < 1 || v > 100000) return false;
    out = static_cast<int>(v);
    return true;
}

bool parseArgs(int argc, char** argv, Options& o) {
    bool gridGiven = false;
    for (int i = 1; i < argc; ++i) {
        const std::string_view a(argv[i]);
        int value = 0;
        if (i + 1 >= argc && (a == "--grid" || a == "--koraci" || a == "--runde")) return false;
        if (a == "--puno") {
            o.full = true;
            if (!gridGiven) o.grids = {32, 64, 96, 128};
        } else if (a == "--grid") {
            if (!parsePositive(argv[++i], value) || value > 512) return false;
            if (!gridGiven) { o.grids.clear(); gridGiven = true; }
            o.grids.push_back(value);
        } else if (a == "--koraci") {
            if (!parsePositive(argv[++i], o.steps)) return false;
        } else if (a == "--runde") {
            if (!parsePositive(argv[++i], o.rounds)) return false;
        } else if (a == "--bez-jezgara") {
            o.kernels = false;
        } else {
            return false;
        }
    }
    return true;
}

// When smoke_demo is started by double-click, Windows opens a console window just for it and closes the
// window as soon as the program ends, so the results would vanish. In that case (this process is the only
// one attached to the console) wait for Enter. Started from a terminal, it returns at once as before.
void waitIfOwnConsole() {
#if defined(_WIN32)
    DWORD processes[2];
    if (GetConsoleProcessList(processes, 2) == 1) {
        std::cout << "\nPritisni Enter za kraj..." << std::flush;
        std::cin.get();
    }
#endif
}

} // namespace

int main(int argc, char** argv) {
    Options o;
    if (!parseArgs(argc, argv, o)) {
        std::cout << "Koriscenje: smoke_demo [--puno] [--grid N]... [--koraci K] [--runde R] [--bez-jezgara]\n"
                  << "  zadano (brzi mod): mreza 64^3, 100 koraka, 3 runde, pa poredjenje tehnika na 64^3 (~20 s)\n"
                  << "  --puno: mreze 32, 64, 96 i 128 (nekoliko minuta)\n"
                  << "  --bez-jezgara: samo cijela simulacija (bez dijela 2)\n";
        waitIfOwnConsole();
        return 2;
    }
    std::cout << "==========================================================================================================\n"
              << " smoke_demo: 3D simulacija dima na mrezi celija (Eulerov pristup), serijska verzija (jedna nit)\n"
              << " Poredjenje: Neoptimizirana verzija (M3) i Optimizirana verzija (M4)\n"
              << "==========================================================================================================\n"
              << "AVX2 na ovom procesoru: "
              << (smoke::avx2Supported() ? "da (verzija C koristi AVX2 putanju)"
                                         : "ne, procesor ili gradnja bez AVX2 (verzija C koristi rezervnu SSE putanju)")
              << "\nKompajler: " <<
#if defined(__clang__)
                 "Clang " __clang_version__
#elif defined(__GNUC__)
                 "GNU " __VERSION__
#elif defined(_MSC_VER)
                 "MSVC"
#else
                 "unknown"
#endif
              << ", oznake: " << SMOKE_DEMO_FLAGS << ", commit: " << SMOKE_DEMO_COMMIT << "\n"
              << "Mod: " << (o.full ? "puni (--puno)" : "brzi (za sve mreze: --puno)") << "\n"
              << "Za vrijeme mjerenja racunar treba biti miran (bez igara, videa, kompajliranja).\n";

    std::cout << "\n==========================================================================================================\n"
              << " DIO 1: Cijela simulacija, ista pocetna stanja i parametri (izvor dima, uzgon, vorticity eps = 5,\n"
              << "        40 Jacobi iteracija, dt = 1/60 s). Vrijeme svih faza koraka; S = ubrzanje (koliko puta brze).\n"
              << "==========================================================================================================\n";
    std::vector<GridSummary> summary;
    for (const int n : o.grids) summary.push_back(simulateGrid(n, o));

    if (o.kernels) {
        smoke_lab::LabOptions lab;
        lab.grids = o.full ? std::vector<int>{32, 64, 96, 128} : std::vector<int>{64};
        lab.rounds = o.rounds;
        std::cout << "\n==========================================================================================================\n"
                  << " DIO 2: Poredjenje tehnika po jezgrima (samo jedno jezgro, bez rubnih uslova)\n"
                  << "==========================================================================================================\n";
        smoke_lab::runJacobi(lab);
        std::cout << "\n";
        smoke_lab::runAdvection(lab);
        smoke_lab::printReadingNote();
    }

    printWhatChanged();

    std::cout << "\n==========================================================================================================\n"
              << " ZAKLJUCAK: ubrzanje cijelog koraka Optimizirane verzije (C)\n"
              << "==========================================================================================================\n";
    for (const GridSummary& s : summary)
        std::cout << "  Mreza " << std::setw(3) << s.n << "^3:  " << std::fixed << std::setprecision(2) << s.speedupVsA
                  << "x brze od Neoptimizirane verzije (A),  " << s.speedupVsB << "x brze od M3 + auto-vektorizacije (B)\n"
                  << std::defaultfloat;
    std::cout << "  Rezultat (gustina dima) je u svim verzijama bit po bit isti (vidi kolonu 'tacnost' u dijelu 1).\n";
    waitIfOwnConsole();
    return 0;
}
