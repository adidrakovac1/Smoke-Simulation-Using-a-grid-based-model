// smoke_lab: side-by-side comparison of kernel versions, in the style of the lab example
// (docs/reference/PRS_Vektorizacija: BenchmarkRunner::run(), measure(), check_vector). The comparison itself is
// in LabRunner.cpp (shared with smoke_demo). Output is ASCII Bosnian (decision D7).

#include "CpuFeatures.h"
#include "LabRunner.h"

#include <cstdlib>
#include <iostream>
#include <string_view>

#ifndef SMOKE_LAB_COMMIT
#define SMOKE_LAB_COMMIT "unknown"
#endif
#ifndef SMOKE_LAB_FLAGS
#define SMOKE_LAB_FLAGS "unknown"
#endif

namespace {

struct Options {
    smoke_lab::LabOptions lab;
    bool jacobi = true;
    bool advection = true;
};

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
            if (!gridGiven) { o.lab.grids.clear(); gridGiven = true; }
            o.lab.grids.push_back(value);
        } else if (a == "--kernel") {
            const std::string_view k(argv[++i]);
            if (k == "jacobi")         { o.jacobi = true;  o.advection = false; }
            else if (k == "advekcija") { o.jacobi = false; o.advection = true; }
            else if (k == "sve")       { o.jacobi = true;  o.advection = true; }
            else return false;
        }
        else if (a == "--rounds")  { if (!parsePositive(argv[++i], o.lab.rounds)) return false; }
        else if (a == "--repeats") { if (!parsePositive(argv[++i], o.lab.repeats)) return false; }
        else if (a == "--warmup")  { if (!parsePositive(argv[++i], o.lab.warmup)) return false; }
        else if (a == "--iters")   { if (!parsePositive(argv[++i], o.lab.iterations)) return false; }
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
    if (o.jacobi) smoke_lab::runJacobi(o.lab);
    if (o.jacobi && o.advection) std::cout << "\n";
    if (o.advection) smoke_lab::runAdvection(o.lab);
    smoke_lab::printReadingNote();
    return 0;
}
