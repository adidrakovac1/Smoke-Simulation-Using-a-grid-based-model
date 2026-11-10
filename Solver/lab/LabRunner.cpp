// Kernel comparison tables for smoke_lab and smoke_demo (see LabRunner.h).
//
// For every kernel all versions are kept as separate functions. Each version is first checked against
// version 1 (bit by bit), then measured: warm-up runs, then `repeats` timed runs (clock started before and
// stopped after each run), reporting median and minimum. S is the speedup relative to version 1, and
// "poboljsanje" is the improvement relative to the version named in brackets (usually the previous one).
//
// This shows the speedup of ONE KERNEL in a few seconds (for demonstrations). The speedup of the whole
// simulation step, with the full protocol (interleaved rounds, decision D10), is in docs/plan/PERF_LOG.md.
// Output is ASCII Bosnian (decision D7).

#include "LabRunner.h"

#include "LabKernels.h"
#include "stages/Stages.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace smoke_lab {
namespace {

using smoke::Field3D;
using smoke::GridSize;

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
Sample measure(const LabOptions& o, Run run, Observe observe) {
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
// parents[v] is the version the "poboljsanje" column compares v with (-1: the previous row).
struct Case {
    std::vector<std::string> names;
    std::vector<int> parents;
    std::function<void()> reset;  // restores the inputs before each version is checked or measured
    std::function<bool(std::size_t v)> run;
    std::function<std::vector<const Field3D*>()> outputs;
};

constexpr int kNameWidth = 41;

void compareVersions(const LabOptions& o, int n, Case& c) {
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
              << "  " << std::left << std::setw(kNameWidth) << "verzija" << std::right << std::setw(11) << "medijan ms"
              << std::setw(11) << "minimum ms" << std::setw(9) << "raspon" << std::setw(10) << "S (od 1.)"
              << std::setw(17) << "poboljsanje" << "   tacnost\n";
    std::vector<double> med(count, 0.0);
    for (std::size_t v = 0; v < count; ++v) {
        if (!available[v]) {
            std::cout << "  " << std::left << std::setw(kNameWidth) << c.names[v] << std::right
                      << "  nije dostupno (procesor ili gradnja bez AVX2)\n";
            continue;
        }
        const std::vector<double>& rm = roundMedians[v];
        med[v] = medianOf(rm);
        const double spread = *std::max_element(rm.begin(), rm.end()) - *std::min_element(rm.begin(), rm.end());
        std::cout << "  " << std::left << std::setw(kNameWidth) << c.names[v] << std::right << std::fixed
                  << std::setprecision(3) << std::setw(11) << med[v] << std::setw(11) << minimum[v] << std::setw(9)
                  << spread << std::setw(9) << std::setprecision(2) << med[0] / med[v] << "x";
        const int parent = (v == 0) ? -2 : (c.parents[v] >= 0 ? c.parents[v] : static_cast<int>(v) - 1);
        if (parent < 0 || !available[static_cast<std::size_t>(parent)]) {
            std::cout << std::setw(17) << "-";
        } else {
            const double base = med[static_cast<std::size_t>(parent)];
            std::cout << std::setw(7) << std::setprecision(1) << 100.0 * (base - med[v]) / base << " % (od "
                      << parent + 1 << ".)";
        }
        std::cout << "   " << accuracy[v] << "\n" << std::defaultfloat;
    }
}

void printMeasurementNote(const LabOptions& o) {
    std::cout << "Broj naizmjenicnih rundi: " << o.rounds << "; u svakoj rundi svaka verzija: " << o.warmup
              << " zagrijavanje + " << o.repeats << " mjerenja.\n"
              << "Medijan = medijan medijana rundi; minimum = najbrze pojedinacno mjerenje;\n"
              << "raspon = najveci - najmanji medijan runde (sum).\n";
}

// ---------------------------------------------------------------------------------------------------------
// Jacobi. One iteration writes pNew from pOld; returns false when the version is not available here.
using JacobiFn = std::function<bool(Field3D& pNew, const Field3D& pOld, const Field3D& div)>;

struct JacobiVersion {
    std::string name;
    int parent;  // version number (0-based) for the "poboljsanje" column; -1 = previous row
    JacobiFn fn;
};

std::vector<JacobiVersion> jacobiVersions() {
    using smoke::serial::KernelIsa;
    using smoke::serial::jacobiIteration;
    using smoke::serial::wholeInterior;
    return {
        {"1. M3 kod, bez vektorizacije (B0)", -1,
         [](Field3D& n, const Field3D& o, const Field3D& d) { smoke_lab::jacobiM3Scalar(n, o, d); return true; }},
        {"2. M3, rucno odmotano 4x, bez vekt. (T3)", 0,
         [](Field3D& n, const Field3D& o, const Field3D& d) {
             smoke_lab::jacobiM3ScalarUnrolled(n, o, d);
             return true;
         }},
        {"3. M3 kod, auto-vektorizacija (B1, SSE)", 0,
         [](Field3D& n, const Field3D& o, const Field3D& d) { smoke_lab::jacobiM3Auto(n, o, d); return true; }},
        {"4. restrict, funkcija po redu (M4-007)", -1,
         [](Field3D& n, const Field3D& o, const Field3D& d) {
             return jacobiIteration(n, o, d, wholeInterior(n.size()), KernelIsa::Scalar);
         }},
        {"5. AVX2 po fajlu, -mavx2 (M4-009)", -1,
         [](Field3D& n, const Field3D& o, const Field3D& d) {
             return jacobiIteration(n, o, d, wholeInterior(n.size()), KernelIsa::Avx2);
         }},
        {"6. AVX2 intrinsics, rucno (M4-010)", -1,
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

// ---------------------------------------------------------------------------------------------------------
// Advection. One measured run = the 5 advections of one simulation step: u, v and w carried by (u, v, w)
// itself (advectVelocity, so the source is also one of the velocity arrays), then density and temperature
// carried by the same velocity (advectScalars). setBoundary is not called, so only the kernel is measured.
using AdvectFn = std::function<bool(Field3D& dst, const Field3D& src, const Field3D& u, const Field3D& v,
                                    const Field3D& w, float dt)>;

enum class AdvectKind { PerField, AoS, Fused };

struct AdvectVersion {
    std::string name;
    int parent;  // as JacobiVersion::parent
    AdvectKind kind;
    AdvectFn fn;  // only for PerField
};

std::vector<AdvectVersion> advectionVersions() {
    using smoke::serial::KernelIsa;
    using smoke::serial::advectScalar;
    using smoke::serial::wholeInterior;
    return {
        {"1. M3 kod, SoA, bez vektorizacije (B0)", -1, AdvectKind::PerField,
         [](Field3D& d, const Field3D& s, const Field3D& u, const Field3D& v, const Field3D& w, float dt) {
             smoke_lab::advectM3Scalar(d, s, u, v, w, dt);
             return true;
         }},
        {"2. M3 kod, SoA, zadane oznake (B1)", -1, AdvectKind::PerField,
         [](Field3D& d, const Field3D& s, const Field3D& u, const Field3D& v, const Field3D& w, float dt) {
             smoke_lab::advectM3Auto(d, s, u, v, w, dt);
             return true;
         }},
        {"3. M3 kod, AoS (struct po celiji)", 1, AdvectKind::AoS, AdvectFn{}},
        {"4. vektorizirano SoA, SSE (M4-011..013)", 1, AdvectKind::PerField,
         [](Field3D& d, const Field3D& s, const Field3D& u, const Field3D& v, const Field3D& w, float dt) {
             return advectScalar(d, s, u, v, w, dt, wholeInterior(d.size()), KernelIsa::Scalar);
         }},
        {"5. AVX2 po fajlu, -mavx2 (M4-014)", -1, AdvectKind::PerField,
         [](Field3D& d, const Field3D& s, const Field3D& u, const Field3D& v, const Field3D& w, float dt) {
             return advectScalar(d, s, u, v, w, dt, wholeInterior(d.size()), KernelIsa::Avx2);
         }},
        {"6. spojeno, jedan trag (M4-015..017)", -1, AdvectKind::Fused, AdvectFn{}},
    };
}

} // namespace

void runJacobi(const LabOptions& o) {
    std::cout << "=== Jezgro: Jacobi iteracija (projekcija pritiska) ===\n"
              << "Jedno mjerenje = " << o.iterations << " Jacobi iteracija (kao u jednom koraku simulacije).\n";
    printMeasurementNote(o);
    const std::vector<JacobiVersion> versions = jacobiVersions();
    for (const int n : o.grids) {
        const GridSize g{n, n, n};
        Field3D start(g), div(g), a, b;
        fillRandom(start, 42);
        fillRandom(div, 7);
        Case c;
        for (const JacobiVersion& v : versions) {
            c.names.push_back(v.name);
            c.parents.push_back(v.parent);
        }
        c.reset = [&] {
            copyField(a, start);
            copyField(b, start);
        };
        c.run = [&](std::size_t v) { return runIterations(versions[v].fn, a, b, div, o.iterations); };
        c.outputs = [&] { return std::vector<const Field3D*>{&a}; };
        compareVersions(o, n, c);
    }
}

void runAdvection(const LabOptions& o) {
    // dt = 2 with velocities in [-1, 1): back-traced points move up to 2 cells, so near the walls the clamp
    // is reached and both of its limits are exercised.
    const float dt = 2.0f;
    std::cout << "=== Jezgro: semi-Lagrangeova advekcija ===\n"
              << "Jedno mjerenje = 5 advekcija (u, v, w, gustina, temperatura;\n"
              << "kao u jednom koraku simulacije).\n";
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

        // AoS copies of the same inputs (packed once, not timed) and the AoS output.
        const std::size_t cells = u.totalFloats();
        std::vector<CellAoS> aosSrc(cells), aosDst(cells);
        for (std::size_t i = 0; i < cells; ++i)
            for (int q = 0; q < kAoSQuantities; ++q) aosSrc[i].q[q] = sources[q]->data()[i];
        for (CellAoS& cell : aosDst)
            for (float& x : cell.q) x = 0.0f;  // ghost cells stay 0, as in the SoA outputs
        bool lastWasAoS = false;

        Case c;
        for (const AdvectVersion& ver : versions) {
            c.names.push_back(ver.name);
            c.parents.push_back(ver.parent);
        }
        c.reset = [] {};  // the inputs are only read
        c.run = [&](std::size_t ver) {
            lastWasAoS = versions[ver].kind == AdvectKind::AoS;
            switch (versions[ver].kind) {
                case AdvectKind::AoS:
                    advectM3AoS(aosDst.data(), aosSrc.data(), g, u.strideJ(), u.strideK(), dt);
                    return true;
                case AdvectKind::Fused: {  // as advectVelocity and advectScalars since M4-015/M4-016 (Auto build)
                    Field3D* const d3[3] = {&out[0], &out[1], &out[2]};
                    Field3D* const d2[2] = {&out[3], &out[4]};
                    const Field3D* const s2[2] = {&density, &temperature};
                    return smoke::serial::advectFields(d3, sources, 3, u, v, w, dt, smoke::serial::wholeInterior(g)) &&
                           smoke::serial::advectFields(d2, s2, 2, u, v, w, dt, smoke::serial::wholeInterior(g));
                }
                case AdvectKind::PerField:
                    for (std::size_t f = 0; f < out.size(); ++f)
                        if (!versions[ver].fn(out[f], *sources[f], u, v, w, dt)) return false;
                    return true;
            }
            return false;
        };
        c.outputs = [&] {
            if (lastWasAoS)  // unpack the AoS result (outside the timed region) so it can be compared
                for (std::size_t i = 0; i < cells; ++i)
                    for (int q = 0; q < kAoSQuantities; ++q) out[static_cast<std::size_t>(q)].data()[i] = aosDst[i].q[q];
            std::vector<const Field3D*> p;
            for (const Field3D& f : out) p.push_back(&f);
            return p;
        };
        compareVersions(o, n, c);
    }
}

void printReadingNote() {
    std::cout << "\nKako citati: razlika izmedju dvije verzije je pouzdana tek kad je veca od raspona rundi\n"
              << "(odluka D10). Za cijeli korak simulacije vidi docs/plan/PERF_LOG.md.\n";
}

} // namespace smoke_lab
