// smoke_lab: side-by-side comparison of kernel versions, in the style of the lab example
// (docs/reference/PRS_Vektorizacija: BenchmarkRunner::run(), measure(), check_vector).
//
// For every kernel all versions are kept as separate functions. Each version is first checked against
// version 1 (bit by bit), then measured: warm-up runs, then `repeats` timed runs (clock started before and
// stopped after each run), reporting median and minimum. S is the speedup relative to version 1, and
// "prema prethodnoj" is the improvement relative to the previous row.
//
// This shows the speedup of ONE KERNEL in a few seconds (for demonstrations). The speedup of the whole
// simulation step, with the full protocol (interleaved rounds, decision D10), is in docs/plan/PERF_LOG.md.
// Output is ASCII Bosnian (decision D7).

#include "CpuFeatures.h"
#include "LabKernels.h"
#include "stages/Stages.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iomanip>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#ifndef SMOKE_LAB_COMMIT
#define SMOKE_LAB_COMMIT "unknown"
#endif
#ifndef SMOKE_LAB_FLAGS
#define SMOKE_LAB_FLAGS "unknown"
#endif

namespace {

using smoke::Field3D;
using smoke::GridSize;

struct Options {
    std::vector<int> grids{32, 64, 96, 128};
    int rounds = 3;       // interleaved rounds (decision D10)
    int repeats = 10;
    int warmup = 1;
    int iterations = 40;  // Jacobi iterations per run, as in one simulation step (SmokeParams default)
    bool jacobi = true;
    bool advection = true;
};

double medianOf(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    const std::size_t m = v.size() / 2;
    return (v.size() % 2) ? v[m] : 0.5 * (v[m - 1] + v[m]);
}

struct Sample {
    double medianMs = 0.0;
    double minMs = 0.0;
};

// Keeps a result "used" so the compiler cannot drop the measured work (like perf::consume in the lab).
void consume(double value) {
    static_cast<void>(std::atomic<double>(value).load(std::memory_order_relaxed));
    std::atomic_signal_fence(std::memory_order_seq_cst);
}

double checksum(const Field3D& f) {
    double sum = 0.0;
    const float* p = f.data();
    for (std::size_t n = 0; n < f.totalFloats(); ++n) sum += p[n];
    return sum;
}

// Same scheme as perf::measure in the lab: warm-ups, then repeats with the clock around run() only.
template <class Run, class Observe>
Sample measure(const Options& o, Run run, Observe observe) {
    using Clock = std::chrono::steady_clock;
    for (int i = 0; i < o.warmup; ++i) { run(); consume(observe()); }
    std::vector<double> ms;
    for (int i = 0; i < o.repeats; ++i) {
        std::atomic_signal_fence(std::memory_order_seq_cst);
        const auto start = Clock::now();
        run();
        const auto stop = Clock::now();
        std::atomic_signal_fence(std::memory_order_seq_cst);
        consume(observe());
        ms.push_back(std::chrono::duration<double, std::milli>(stop - start).count());
    }
    std::sort(ms.begin(), ms.end());
    const std::size_t m = ms.size() / 2;
    return {(ms.size() % 2) ? ms[m] : 0.5 * (ms[m - 1] + ms[m]), ms.front()};
}

void copyField(Field3D& dst, const Field3D& src) {
    dst.resize(src.size());
    std::memcpy(dst.data(), src.data(), src.totalFloats() * sizeof(float));
}

long long cellsDifferent(const Field3D& a, const Field3D& b) {
    long long n = 0;
    for (std::size_t i = 0; i < a.totalFloats(); ++i) n += std::memcmp(a.data() + i, b.data() + i, sizeof(float)) != 0;
    return n;
}

// Deterministic pseudo-random values in [-1, 1) (SplitMix64; no random numbers inside the solver step).
void fillRandom(Field3D& f, std::uint64_t seed) {
    float* p = f.data();
    for (std::size_t n = 0; n < f.totalFloats(); ++n) {
        std::uint64_t z = (seed += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        z ^= z >> 31;
        p[n] = static_cast<float>(z >> 40) * (2.0f / 16777216.0f) - 1.0f;
    }
}

// ---------------------------------------------------------------------------------------------------------
// Generic comparison, the same for every kernel. For one grid, a Case holds the inputs and outputs of all
// versions: run(v) does one measured run of version v and returns false when that version is not available
// here (CPU or build without AVX2); outputs() are the fields compared bit by bit against version 1.
struct Case {
    std::vector<std::string> names;
    std::function<void()> reset;  // restores the inputs before each version is checked or measured
    std::function<bool(std::size_t v)> run;
    std::function<std::vector<const Field3D*>()> outputs;
};

void compareVersions(const Options& o, int n, Case& c) {
    const std::size_t count = c.names.size();

    // 1. Accuracy first (like check_vector in the lab): every version against version 1, bit by bit.
    std::vector<bool> available(count, false);
    std::vector<std::string> accuracy(count);
    std::vector<Field3D> expected;
    for (std::size_t v = 0; v < count; ++v) {
        c.reset();
        if (!c.run(v)) continue;
        available[v] = true;
        const std::vector<const Field3D*> out = c.outputs();
        if (v == 0) {
            for (const Field3D* f : out) {
                expected.emplace_back();
                copyField(expected.back(), *f);
            }
            accuracy[v] = "referenca";
            continue;
        }
        long long diff = 0;
        for (std::size_t f = 0; f < out.size(); ++f) diff += cellsDifferent(*out[f], expected[f]);
        accuracy[v] = diff == 0 ? "bit po bit isto" : "RAZLIKA u " + std::to_string(diff) + " celija";
    }

    // 2. Interleaved rounds; the order of the versions is rotated every round (as perf/run_rounds.sh).
    std::vector<std::vector<double>> roundMedians(count);
    std::vector<double> minimum(count, 1e300);
    for (int r = 0; r < o.rounds; ++r)
        for (std::size_t step = 0; step < count; ++step) {
            const std::size_t v = (step + static_cast<std::size_t>(r)) % count;
            if (!available[v]) continue;
            c.reset();
            const Sample s = measure(o, [&] { c.run(v); }, [&] {
                double sum = 0.0;
                for (const Field3D* f : c.outputs()) sum += checksum(*f);
                return sum;
            });
            roundMedians[v].push_back(s.medianMs);
            minimum[v] = std::min(minimum[v], s.minMs);
        }

    // 3. Table.
    std::cout << "\nMreza " << n << "^3\n"
              << "  " << std::left << std::setw(42) << "verzija" << std::right << std::setw(11) << "medijan ms"
              << std::setw(11) << "minimum ms" << std::setw(13) << "raspon rundi" << std::setw(10) << "S (od 1.)"
              << std::setw(18) << "prema prethodnoj" << "   tacnost\n";
    double firstMedian = 0.0, previousMedian = 0.0;
    for (std::size_t v = 0; v < count; ++v) {
        if (!available[v]) {
            std::cout << "  " << std::left << std::setw(42) << c.names[v] << std::right
                      << "  nije dostupno (procesor ili gradnja bez AVX2)\n";
            continue;
        }
        const std::vector<double>& rm = roundMedians[v];
        const double med = medianOf(rm);
        const double spread = *std::max_element(rm.begin(), rm.end()) - *std::min_element(rm.begin(), rm.end());
        if (v == 0) firstMedian = med;
        std::cout << "  " << std::left << std::setw(42) << c.names[v] << std::right << std::fixed
                  << std::setprecision(3) << std::setw(11) << med << std::setw(11) << minimum[v] << std::setw(13)
                  << spread << std::setw(9) << std::setprecision(2) << firstMedian / med << "x";
        if (v == 0) std::cout << std::setw(18) << "-";
        else std::cout << std::setw(16) << std::setprecision(1) << 100.0 * (previousMedian - med) / previousMedian << " %";
        std::cout << "   " << accuracy[v] << "\n" << std::defaultfloat;
        previousMedian = med;
    }
}

void printMeasurementNote(const Options& o) {
    std::cout << o.rounds << " naizmjenicne runde; u svakoj rundi svaka verzija: " << o.warmup << " zagrijavanje + "
              << o.repeats << " mjerenja.\nMedijan = medijan medijana rundi; minimum = najbrze pojedinacno "
              << "mjerenje; raspon rundi = najveci - najmanji medijan runde (sum).\n";
}

// ---------------------------------------------------------------------------------------------------------
// Jacobi. One iteration writes pNew from pOld; returns false when the version is not available here.
using JacobiFn = std::function<bool(Field3D& pNew, const Field3D& pOld, const Field3D& div)>;

struct JacobiVersion {
    std::string name;
    JacobiFn fn;
};

std::vector<JacobiVersion> jacobiVersions() {
    using smoke::serial::KernelIsa;
    using smoke::serial::jacobiIteration;
    using smoke::serial::wholeInterior;
    return {
        {"1. M3 kod, bez vektorizacije (B0)",
         [](Field3D& n, const Field3D& o, const Field3D& d) { smoke_lab::jacobiM3Scalar(n, o, d); return true; }},
        {"2. M3 kod, auto-vektorizacija (B1, SSE)",
         [](Field3D& n, const Field3D& o, const Field3D& d) { smoke_lab::jacobiM3Auto(n, o, d); return true; }},
        {"3. restrict, funkcija po redu (M4-007)",
         [](Field3D& n, const Field3D& o, const Field3D& d) {
             return jacobiIteration(n, o, d, wholeInterior(n.size()), KernelIsa::Scalar);
         }},
        {"4. AVX2 po fajlu, -mavx2 (M4-009)",
         [](Field3D& n, const Field3D& o, const Field3D& d) {
             return jacobiIteration(n, o, d, wholeInterior(n.size()), KernelIsa::Avx2);
         }},
        {"5. AVX2 intrinsics, rucno (M4-010)",
         [](Field3D& n, const Field3D& o, const Field3D& d) {
             return jacobiIteration(n, o, d, wholeInterior(n.size()), KernelIsa::Avx2Intrinsics);
         }},
    };
}

// Runs `iterations` Jacobi iterations with double buffering, as project() does (without setBoundary,
// so only the kernel is measured). The result ends up in `a`.
bool runIterations(const JacobiFn& fn, Field3D& a, Field3D& b, const Field3D& div, int iterations) {
    for (int it = 0; it < iterations; ++it) {
        a.swap(b);
        if (!fn(a, b, div)) return false;
    }
    return true;
}

void runJacobi(const Options& o) {
    std::cout << "=== Jezgro: Jacobi iteracija (projekcija pritiska) ===\n"
              << "Jedno mjerenje = " << o.iterations << " Jacobi iteracija (kao u jednom koraku simulacije). ";
    printMeasurementNote(o);
    const std::vector<JacobiVersion> versions = jacobiVersions();
    for (const int n : o.grids) {
        const GridSize g{n, n, n};
        Field3D start(g), div(g), a, b;
        fillRandom(start, 42);
        fillRandom(div, 7);
        Case c;
        for (const JacobiVersion& v : versions) c.names.push_back(v.name);
        c.reset = [&] {
            copyField(a, start);
            copyField(b, start);
        };
        c.run = [&](std::size_t v) { return runIterations(versions[v].fn, a, b, div, o.iterations); };
        c.outputs = [&] { return std::vector<const Field3D*>{&a}; };
        compareVersions(o, n, c);
    }
}

// ---------------------------------------------------------------------------------------------------------
// Advection. One measured run = the 5 advections of one simulation step: u, v and w carried by (u, v, w)
// itself (advectVelocity, so the source is also one of the velocity arrays), then density and temperature
// carried by the same velocity (advectScalars). setBoundary is not called, so only the kernel is measured.
using AdvectFn = std::function<bool(Field3D& dst, const Field3D& src, const Field3D& u, const Field3D& v,
                                    const Field3D& w, float dt)>;

struct AdvectVersion {
    std::string name;
    AdvectFn fn;
};

std::vector<AdvectVersion> advectionVersions() {
    using smoke::serial::KernelIsa;
    using smoke::serial::advectScalar;
    using smoke::serial::wholeInterior;
    return {
        {"1. M3 kod, bez vektorizacije (B0)",
         [](Field3D& d, const Field3D& s, const Field3D& u, const Field3D& v, const Field3D& w, float dt) {
             smoke_lab::advectM3Scalar(d, s, u, v, w, dt);
             return true;
         }},
        {"2. M3 kod, zadane oznake (B1)",
         [](Field3D& d, const Field3D& s, const Field3D& u, const Field3D& v, const Field3D& w, float dt) {
             smoke_lab::advectM3Auto(d, s, u, v, w, dt);
             return true;
         }},
        {"3. vektorizirano, SSE (M4-011..013)",
         [](Field3D& d, const Field3D& s, const Field3D& u, const Field3D& v, const Field3D& w, float dt) {
             return advectScalar(d, s, u, v, w, dt, wholeInterior(d.size()), KernelIsa::Scalar);
         }},
        {"4. AVX2 po fajlu, -mavx2 (M4-014)",
         [](Field3D& d, const Field3D& s, const Field3D& u, const Field3D& v, const Field3D& w, float dt) {
             return advectScalar(d, s, u, v, w, dt, wholeInterior(d.size()), KernelIsa::Avx2);
         }},
    };
}

void runAdvection(const Options& o) {
    // dt = 2 with velocities in [-1, 1): back-traced points move up to 2 cells, so near the walls the clamp
    // is reached and both of its limits are exercised.
    const float dt = 2.0f;
    std::cout << "=== Jezgro: semi-Lagrangeova advekcija ===\n"
              << "Jedno mjerenje = 5 advekcija (u, v, w, gustina, temperatura; kao u jednom koraku simulacije). ";
    printMeasurementNote(o);
    const std::vector<AdvectVersion> versions = advectionVersions();
    for (const int n : o.grids) {
        const GridSize g{n, n, n};
        Field3D u(g), v(g), w(g), density(g), temperature(g);
        fillRandom(u, 11);
        fillRandom(v, 12);
        fillRandom(w, 13);
        fillRandom(density, 14);
        fillRandom(temperature, 15);
        std::vector<Field3D> out(5);
        for (Field3D& f : out) f.resize(g);
        const Field3D* sources[5] = {&u, &v, &w, &density, &temperature};
        Case c;
        for (const AdvectVersion& ver : versions) c.names.push_back(ver.name);
        c.reset = [] {};  // the inputs are only read
        c.run = [&](std::size_t ver) {
            for (std::size_t f = 0; f < out.size(); ++f)
                if (!versions[ver].fn(out[f], *sources[f], u, v, w, dt)) return false;
            return true;
        };
        c.outputs = [&] {
            std::vector<const Field3D*> p;
            for (const Field3D& f : out) p.push_back(&f);
            return p;
        };
        compareVersions(o, n, c);
    }
}

bool parsePositive(const char* text, int& out) {
    char* end = nullptr;
    const long v = std::strtol(text, &end, 10);
    if (end == text || *end != '\0' || v < 1 || v > 4096) return false;
    out = static_cast<int>(v);
    return true;
}

bool parseArgs(int argc, char** argv, Options& o) {
    bool gridGiven = false;
    for (int i = 1; i < argc; ++i) {
        const std::string_view a(argv[i]);
        int value = 0;
        if (i + 1 >= argc && (a == "--grid" || a == "--rounds" || a == "--repeats" || a == "--warmup" ||
                              a == "--iters" || a == "--kernel"))
            return false;
        if (a == "--grid") {
            if (!parsePositive(argv[++i], value)) return false;
            if (!gridGiven) { o.grids.clear(); gridGiven = true; }
            o.grids.push_back(value);
        } else if (a == "--kernel") {
            const std::string_view k(argv[++i]);
            if (k == "jacobi")         { o.jacobi = true;  o.advection = false; }
            else if (k == "advekcija") { o.jacobi = false; o.advection = true; }
            else if (k == "sve")       { o.jacobi = true;  o.advection = true; }
            else return false;
        }
        else if (a == "--rounds")  { if (!parsePositive(argv[++i], o.rounds)) return false; }
        else if (a == "--repeats") { if (!parsePositive(argv[++i], o.repeats)) return false; }
        else if (a == "--warmup")  { if (!parsePositive(argv[++i], o.warmup)) return false; }
        else if (a == "--iters")   { if (!parsePositive(argv[++i], o.iterations)) return false; }
        else return false;
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    Options o;
    if (!parseArgs(argc, argv, o)) {
        std::cout << "Koriscenje: smoke_lab [--kernel jacobi|advekcija|sve] [--grid N]... [--rounds R] [--repeats M]\n"
                  << "                     [--warmup W] [--iters K]\n"
                  << "  zadano: sva jezgra, mreze 32 64 96 128, 3 runde, 10 mjerenja, 1 zagrijavanje,\n"
                  << "          40 Jacobi iteracija po mjerenju\n";
        return 2;
    }
    std::cout << "=== smoke_lab: poredjenje verzija jezgara (serijski, jedna nit) ===\n"
              << "AVX2 na ovom procesoru: " << (smoke::avx2Supported() ? "da" : "ne (ili gradnja bez AVX2)") << "\n"
              << "Kompajler: " <<
#if defined(__clang__)
                 "Clang " __clang_version__
#elif defined(__GNUC__)
                 "GNU " __VERSION__
#elif defined(_MSC_VER)
                 "MSVC"
#else
                 "unknown"
#endif
              << ", oznake: " << SMOKE_LAB_FLAGS << ", commit: " << SMOKE_LAB_COMMIT << "\n"
              << "Napomena: ovo je ubrzanje JEDNOG jezgra. Ubrzanje cijelog koraka: docs/plan/PERF_LOG.md.\n"
              << "Za vrijeme mjerenja racunar treba biti miran (bez igara, videa, kompajliranja).\n\n";
    if (o.jacobi) runJacobi(o);
    if (o.jacobi && o.advection) std::cout << "\n";
    if (o.advection) runAdvection(o);
    std::cout << "\nKako citati: razlika izmedju dvije verzije je pouzdana tek kad je veca od raspona rundi\n"
              << "(odluka D10). Za cijeli korak simulacije vidi docs/plan/PERF_LOG.md.\n";
    return 0;
}
