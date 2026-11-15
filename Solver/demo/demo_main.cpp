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
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
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

#if defined(__APPLE__)
#include <cerrno>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;
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
    bool separate = false;
    int version = -1;
    unsigned long launcherId = 0;
    int threads = 0;
    bool parallel = false;
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
void reportProgressStart(int version, const Options& o, bool showProgress) {
    if (showProgress) {
        std::cout << "    " << static_cast<char>('A' + version) << ": " << o.warmup << " koraka zagrijavanja + "
                  << o.steps << " mjerenih koraka ... " << std::flush;
    }
}

void reportProgressComplete(bool showProgress) {
    if (showProgress) std::cout << "gotovo\n" << std::flush;
}

RunResult runM3(int version, const GridSize& g, const Options& o, bool showProgress) {
    smoke::SmokeParams params;  // defaults, as SmokeSolver uses them
    smoke::SmokeState state;
    state.resize(g);
    StepTimings t;
    auto step = [&] {
        if (version == 0) smoke_demo::m3_b0::m3Step(state, params, t);
        else smoke_demo::m3_b1::m3Step(state, params, t);
    };
    reportProgressStart(version, o, showProgress);
    for (int i = 0; i < o.warmup; ++i) step();
    RunResult r;
    for (int i = 0; i < o.steps; ++i) {
        step();
        accumulate(r, t);
    }
    reportProgressComplete(showProgress);
    const smoke::Field3D& d = state.density;
    r.density = packInterior(&d.at(1, 1, 1), g.nx, g.ny, g.nz, d.strideJ(), d.strideK());
    return r;
}

// Version C: the optimized solver through its public interface (the same one smoke_cli and the renderer use).
RunResult runOptimized(const GridSize& g, const Options& o, bool showProgress) {
    smoke::SolverConfig config;
    config.grid = g;
    config.backend = o.parallel ? smoke::Backend::CpuParallel : smoke::Backend::Serial;
    config.threads = o.threads;
    smoke::SmokeSolver solver(config);
    reportProgressStart(2, o, showProgress);
    for (int i = 0; i < o.warmup; ++i) solver.step();
    RunResult r;
    for (int i = 0; i < o.steps; ++i) {
        solver.step();
        accumulate(r, solver.lastTimings());
    }
    reportProgressComplete(showProgress);
    const smoke::DensityView v = solver.density();
    r.density = packInterior(v.origin, v.nx, v.ny, v.nz, v.strideJ, v.strideK);
    return r;
}

RunResult runVersion(int version, const GridSize& g, const Options& o, bool showProgress = false) {
    return version < 2 ? runM3(version, g, o, showProgress) : runOptimized(g, o, showProgress);
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

std::uint64_t densityHash(const std::vector<float>& density) {
    std::uint64_t hash = 14695981039346656037ull;
    for (float value : density) {
        std::uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        for (unsigned int shift = 0; shift < 32; shift += 8) {
            hash ^= (bits >> shift) & 0xffu;
            hash *= 1099511628211ull;
        }
    }
    return hash;
}

bool allFinite(const std::vector<float>& density) {
    return std::all_of(density.begin(), density.end(), [](float value) { return std::isfinite(value); });
}

bool simulateVersion(int n, int version, const Options& o) {
    const GridSize g{n, n, n};
    std::vector<double> totals;
    std::array<std::vector<double>, smoke::kStageCount> stages;
    std::vector<float> finalDensity;

    std::cout << "\nMreza " << n << "^3 (" << static_cast<long long>(n) * n * n << " celija), "
              << o.steps << " izmjerenih koraka (+" << o.warmup << " zagrijavanje), broj rundi: " << o.rounds
              << "\n";
    for (int round = 0; round < o.rounds; ++round) {
        std::cout << "  runda " << round + 1 << "/" << o.rounds << " ...\n" << std::flush;
        RunResult result = runVersion(version, g, o, true);
        totals.push_back(result.totalMs);
        for (std::size_t stage = 0; stage < smoke::kStageCount; ++stage)
            stages[stage].push_back(result.stageMs[stage]);
        if (round == 0) finalDensity = std::move(result.density);
    }

    const double medianTotal = medianOf(totals);
    const double perStep = medianTotal / o.steps;
    std::cout << "\n  Rezultati verzije " << static_cast<char>('A' + version) << ":\n"
              << "  " << std::left << std::setw(26) << "faza" << std::right << std::setw(16) << "ms/korak"
              << "\n";
    for (std::size_t stage = 0; stage < smoke::kStageCount; ++stage)
        std::cout << "  " << std::left << std::setw(26) << smoke::stageName(static_cast<Stage>(stage)) << std::right
                  << std::fixed << std::setprecision(3) << std::setw(16) << medianOf(stages[stage]) / o.steps << "\n";
    std::cout << "  " << std::left << std::setw(26) << "UKUPNO (cijeli korak)" << std::right << std::fixed
              << std::setprecision(3) << std::setw(16) << perStep << "\n"
              << "  Ukupno: " << std::setprecision(3) << medianTotal / 1000.0 << " s; " << std::setprecision(1)
              << 1000.0 / perStep << " koraka/s\n"
              << "  Gustina: " << (allFinite(finalDensity) ? "sve vrijednosti su konacne" : "GRESKA: NaN/Inf")
              << "; FNV-1a: " << std::hex << std::setfill('0') << std::setw(16) << densityHash(finalDensity)
              << std::dec << std::setfill(' ') << "\n";
    std::cout << std::defaultfloat << std::flush;
    return allFinite(finalDensity);
}

#if defined(_WIN32)
std::wstring completionEventName(unsigned long launcherId, int version) {
    return L"Local\\SmokeDemoABC_" + std::to_wstring(launcherId) + L"_" +
           std::to_wstring(version);
}

bool signalVersionComplete(unsigned long launcherId, int version) {
    if (launcherId == 0) return true;
    const std::wstring name = completionEventName(launcherId, version);
    HANDLE event = OpenEventW(EVENT_MODIFY_STATE, FALSE, name.c_str());
    if (event == nullptr) {
        std::cerr << "GRESKA: ne mogu otvoriti signal za verziju " << static_cast<char>('A' + version)
                  << " (Windows greska " << GetLastError() << ").\n";
        return false;
    }
    const BOOL signaled = SetEvent(event);
    const DWORD error = signaled ? ERROR_SUCCESS : GetLastError();
    CloseHandle(event);
    if (!signaled) {
        std::cerr << "GRESKA: ne mogu javiti zavrsetak verzije " << static_cast<char>('A' + version)
                  << " (Windows greska " << error << ").\n";
        return false;
    }
    return true;
}

bool launchSeparateWindows(const Options& o) {
    std::array<HANDLE, kVersions> completionEvents{};
    const unsigned long launcherId = GetCurrentProcessId();
    for (int version = 0; version < kVersions; ++version) {
        const std::wstring name = completionEventName(launcherId, version);
        SetLastError(ERROR_SUCCESS);
        completionEvents[version] = CreateEventW(nullptr, TRUE, FALSE, name.c_str());
        const DWORD error = GetLastError();
        if (completionEvents[version] == nullptr || error == ERROR_ALREADY_EXISTS) {
            std::cerr << "GRESKA: ne mogu pripremiti signal za konzolu " << static_cast<char>('A' + version)
                      << " (Windows greska " << (error == ERROR_SUCCESS ? ERROR_INVALID_HANDLE : error) << ").\n";
            for (HANDLE event : completionEvents)
                if (event != nullptr) CloseHandle(event);
            return false;
        }
    }

    std::array<wchar_t, 32768> executablePath{};
    const DWORD pathLength = GetModuleFileNameW(nullptr, executablePath.data(),
                                                 static_cast<DWORD>(executablePath.size()));
    if (pathLength == 0 || pathLength >= executablePath.size()) {
        std::cerr << "GRESKA: ne mogu pronaci putanju do smoke_demo.exe (Windows greska " << GetLastError()
                  << ").\n";
        for (HANDLE event : completionEvents)
            if (event != nullptr) CloseHandle(event);
        return false;
    }

    bool success = true;
    for (int version = 0; version < kVersions; ++version) {
        std::wstring command = L"\"" + std::wstring(executablePath.data(), pathLength) + L"\" --verzija ";
        command += static_cast<wchar_t>(L'A' + version);
        command += L" --launcher-id " + std::to_wstring(launcherId);
        if (o.full) command += L" --puno";
        for (int grid : o.grids) command += L" --grid " + std::to_wstring(grid);
        command += L" --koraci " + std::to_wstring(o.steps);
        command += L" --runde " + std::to_wstring(o.rounds);
        if (o.threads > 0) command += L" --niti " + std::to_wstring(o.threads);

        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NEW_CONSOLE, nullptr, nullptr,
                            &startup, &process)) {
            std::cerr << "GRESKA: ne mogu otvoriti konzolu za verziju " << static_cast<char>('A' + version)
                      << " (Windows greska " << GetLastError() << ").\n";
            success = false;
            break;
        }
        CloseHandle(process.hThread);
        std::cout << "Otvorena konzola " << static_cast<char>('A' + version) << "; mjerenje ide redom.\n"
                  << std::flush;

        HANDLE waitHandles[] = {completionEvents[version], process.hProcess};
        const DWORD waitResult = WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE);
        if (waitResult == WAIT_OBJECT_0) {
            CloseHandle(process.hProcess);
            continue;
        }
        DWORD exitCode = 0;
        if (waitResult == WAIT_OBJECT_0 + 1 && GetExitCodeProcess(process.hProcess, &exitCode)) {
            std::cerr << "GRESKA: konzola verzije " << static_cast<char>('A' + version)
                      << " zatvorila se prije zavrsetka mjerenja (kod " << exitCode << ").\n";
        } else {
            std::cerr << "GRESKA: cekanje konzole verzije " << static_cast<char>('A' + version)
                      << " nije uspjelo (Windows greska " << GetLastError() << ").\n";
        }
        CloseHandle(process.hProcess);
        success = false;
        break;
    }

    for (HANDLE event : completionEvents)
        if (event != nullptr) CloseHandle(event);
    return success;
}
#endif

#if defined(__APPLE__)
bool launchSeparateLogsMac(const Options& o, const char* executable) {
    std::error_code pathError;
    const std::filesystem::path executablePath = std::filesystem::absolute(executable, pathError);
    if (pathError) {
        std::cerr << "GRESKA: ne mogu odrediti putanju do smoke_demo: " << pathError.message() << ".\n";
        return false;
    }

    const std::filesystem::path outputDirectory = executablePath.parent_path();
    for (int version = 0; version < kVersions; ++version) {
        const char versionName = static_cast<char>('A' + version);
        const std::filesystem::path logPath =
            outputDirectory / (std::string("smoke_demo_") + versionName + ".txt");

        const std::string executableString = executablePath.string();
        std::vector<std::string> arguments{executableString, "--verzija", std::string(1, versionName)};
        if (o.full) arguments.emplace_back("--puno");
        for (int grid : o.grids) {
            arguments.emplace_back("--grid");
            arguments.push_back(std::to_string(grid));
        }
        arguments.emplace_back("--koraci");
        arguments.push_back(std::to_string(o.steps));
        arguments.emplace_back("--runde");
        arguments.push_back(std::to_string(o.rounds));
        if (o.threads > 0) {
            arguments.emplace_back("--niti");
            arguments.push_back(std::to_string(o.threads));
        }

        std::vector<char*> childArguments;
        childArguments.reserve(arguments.size() + 1);
        for (std::string& argument : arguments) childArguments.push_back(argument.data());
        childArguments.push_back(nullptr);

        posix_spawn_file_actions_t fileActions;
        int error = posix_spawn_file_actions_init(&fileActions);
        const bool fileActionsInitialized = error == 0;
        if (error == 0) {
            error = posix_spawn_file_actions_addopen(&fileActions, STDOUT_FILENO, logPath.c_str(),
                                                     O_WRONLY | O_CREAT | O_TRUNC, 0666);
        }
        if (error == 0) error = posix_spawn_file_actions_adddup2(&fileActions, STDOUT_FILENO, STDERR_FILENO);
        if (error != 0) {
            std::cerr << "GRESKA: ne mogu pripremiti izlaz za verziju " << versionName << " u "
                      << logPath << ": " << std::strerror(error) << ".\n";
            if (fileActionsInitialized) posix_spawn_file_actions_destroy(&fileActions);
            return false;
        }

        std::cout << "Pokrecem verziju " << versionName << "; izlaz: " << logPath << "\n" << std::flush;
        pid_t child = 0;
        error = posix_spawnp(&child, executableString.c_str(), &fileActions, nullptr, childArguments.data(), environ);
        posix_spawn_file_actions_destroy(&fileActions);
        if (error != 0) {
            std::cerr << "GRESKA: ne mogu pokrenuti verziju " << versionName << ": " << std::strerror(error)
                      << ".\n";
            return false;
        }

        int status = 0;
        pid_t waited = 0;
        do {
            waited = waitpid(child, &status, 0);
        } while (waited == -1 && errno == EINTR);
        if (waited == -1) {
            std::cerr << "GRESKA: cekanje verzije " << versionName << " nije uspjelo: "
                      << std::strerror(errno) << ".\n";
            return false;
        }
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            std::cerr << "GRESKA: verzija " << versionName << " nije zavrsila uspjesno; pogledaj " << logPath
                      << ".\n";
            return false;
        }
        std::cout << "Zavrsila verzija " << versionName << ".\n" << std::flush;
    }
    return true;
}
#endif

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
        if (i + 1 >= argc &&
            (a == "--grid" || a == "--koraci" || a == "--runde" || a == "--verzija" || a == "--launcher-id" ||
             a == "--niti" || a == "--threads"))
            return false;
        if (a == "--puno") {
            o.full = true;
            if (!gridGiven) o.grids = {32, 64, 96, 128};
        } else if (a == "--odvojeno") {
            o.separate = true;
        } else if (a == "--niti" || a == "--threads") {
            if (!parsePositive(argv[++i], o.threads)) return false;
            o.parallel = true;
        } else if (a == "--grid") {
            if (!parsePositive(argv[++i], value) || value > 512) return false;
            if (!gridGiven) { o.grids.clear(); gridGiven = true; }
            o.grids.push_back(value);
        } else if (a == "--koraci") {
            if (!parsePositive(argv[++i], o.steps)) return false;
        } else if (a == "--runde") {
            if (!parsePositive(argv[++i], o.rounds)) return false;
        } else if (a == "--verzija") {
            const std::string_view requested(argv[++i]);
            if (requested == "A") o.version = 0;
            else if (requested == "B") o.version = 1;
            else if (requested == "C") o.version = 2;
            else return false;
        } else if (a == "--launcher-id") {
            char* end = nullptr;
            const unsigned long id = std::strtoul(argv[++i], &end, 10);
            if (end == argv[i] || *end != '\0' || id == 0 || id > 0xfffffffful) return false;
            o.launcherId = id;
        } else if (a == "--bez-jezgara") {
            o.kernels = false;
        } else {
            return false;
        }
    }
    return !(o.separate && o.version >= 0) && (o.launcherId == 0 || o.version >= 0);
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
        std::cout << "Koriscenje: smoke_demo [--puno] [--grid N]... [--koraci K] [--runde R] [--niti T] [--bez-jezgara]\n"
                  << "             smoke_demo --odvojeno [--puno] [--grid N]... [--koraci K] [--runde R] [--niti T]\n"
                  << "  zadano (brzi mod): mreza 64^3, 100 koraka, 3 runde, pa poredjenje tehnika na 64^3 (~20 s)\n"
                  << "  --puno: mreze 32, 64, 96 i 128 (nekoliko minuta)\n"
                  << "  --odvojeno: otvori zasebne konzole A/B/C; mjerenja se izvrsavaju redom\n"
                  << "  --niti T: ukljuci paralelni backend za verziju C s T niti (bez opcije: serijski)\n"
                  << "  --bez-jezgara: samo cijela simulacija (bez dijela 2)\n";
        waitIfOwnConsole();
        return 2;
    }
#if defined(_WIN32)
    if (o.separate) {
        std::cout << "Otvaram zasebne konzole za verzije A, B i C. Mjerenja idu redom kako se ne bi ometala.\n"
                  << std::flush;
        const bool launched = launchSeparateWindows(o);
        if (!launched) return 1;
        std::cout << "Sve tri verzije su zavrsile mjerenje; prozori ostaju otvoreni do Enter.\n";
        return 0;
    }
#elif defined(__APPLE__)
    if (o.separate) {
        std::cout << "Pokrecem verzije A, B i C redom; svaki izlaz se cuva u zasebnoj TXT datoteci pored smoke_demo.\n"
                  << std::flush;
        if (!launchSeparateLogsMac(o, argv[0])) return 1;
        std::cout << "Rezultati su sacuvani u smoke_demo_A.txt, smoke_demo_B.txt i smoke_demo_C.txt.\n";
        return 0;
    }
#else
    if (o.separate) {
        std::cerr << "GRESKA: --odvojeno trenutno podrzava samo Windows i macOS.\n";
        return 2;
    }
#endif

    std::cout << "==========================================================================================================\n"
              << " smoke_demo: 3D simulacija dima na mrezi celija (Eulerov pristup)\n"
              << (o.version < 0 ? " Poredjenje: Neoptimizirana verzija (M3) i Optimizirana verzija (M4)\n"
                                : " Samostalni rezultat verzije " + std::string(1, static_cast<char>('A' + o.version)) +
                                      "\n")
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
              << "Backend C: " << (o.parallel ? "CpuParallel" : "Serial")
              << (o.parallel ? " (" + std::to_string(o.threads) + " niti)" : "") << "\n"
              << "Mod: " << (o.full ? "puni (--puno)" : "brzi (za sve mreze: --puno)") << "\n"
              << "Za vrijeme mjerenja racunar treba biti miran (bez igara, videa, kompajliranja).\n";

    if (o.version >= 0) {
        std::cout << "\nVerzija " << static_cast<char>('A' + o.version) << ": " << kVersionNames[o.version]
                  << "\nHash gustine se moze porediti izmedju A, B i C.\n";
        bool finite = true;
        for (const int n : o.grids) finite = simulateVersion(n, o.version, o) && finite;
#if defined(_WIN32)
        if (!signalVersionComplete(o.launcherId, o.version)) return 1;
#endif
        waitIfOwnConsole();
        return finite ? 0 : 1;
    }

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
