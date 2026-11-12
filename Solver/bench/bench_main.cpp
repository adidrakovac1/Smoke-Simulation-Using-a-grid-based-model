#include "smoke/SmokeSolver.h"

// Internal headers, used by --convergence (it needs the pressure and divergence fields) and by --obstacle
// (it prints the sizes of the sparse lists).
#include "SmokeState.h"
#include "stages/ObstacleLists.h"
#include "stages/Stages.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>
#include <cerrno>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <sys/sysctl.h>
#endif

// CPUID is used by --hwinfo (x86 only). GCC and Clang provide <cpuid.h>, MSVC provides <intrin.h>.
#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
#define SMOKE_BENCH_X86 1
#if defined(_MSC_VER)
#include <intrin.h>
#else
#include <cpuid.h>
#endif
#endif

#ifndef SMOKE_BENCH_AVX2_KERNELS
#define SMOKE_BENCH_AVX2_KERNELS "unknown"
#endif
#ifndef SMOKE_BENCH_PER_FILE_FLAGS
#define SMOKE_BENCH_PER_FILE_FLAGS "unknown"
#endif
#ifndef SMOKE_BENCH_BUILD_TYPE
#define SMOKE_BENCH_BUILD_TYPE "unknown"
#endif
#ifndef SMOKE_BENCH_COMPILER_ID
#define SMOKE_BENCH_COMPILER_ID "unknown"
#endif
#ifndef SMOKE_BENCH_COMPILER_VERSION
#define SMOKE_BENCH_COMPILER_VERSION "unknown"
#endif
#ifndef SMOKE_BENCH_VARIANT
#define SMOKE_BENCH_VARIANT "B1"
#endif
#ifndef SMOKE_BENCH_FLAGS
#define SMOKE_BENCH_FLAGS "unknown"
#endif
#ifndef SMOKE_BENCH_COMMIT
#define SMOKE_BENCH_COMMIT "unknown"
#endif

// Local copies of SMOKE_NOINLINE / SMOKE_RESTRICT (Solver/src/Restrict.h): smoke_bench only sees the public
// headers of the solver.
#if defined(__GNUC__) || defined(__clang__)
#define SMOKE_BENCH_NOINLINE __attribute__((noinline))
#define SMOKE_BENCH_RESTRICT __restrict
#elif defined(_MSC_VER)
#define SMOKE_BENCH_NOINLINE __declspec(noinline)
#define SMOKE_BENCH_RESTRICT __restrict
#else
#define SMOKE_BENCH_NOINLINE
#define SMOKE_BENCH_RESTRICT
#endif

namespace {

using Clock = std::chrono::steady_clock;
constexpr double kVerifyRelativeTolerance = 1.0e-4;

struct Options {
    bool calibrate = false;
    bool hwinfo = false;
    bool convergence = false;
    bool convergenceAllSteps = false;
    bool gridExplicit = false;
    bool stepsExplicit = false;
    bool warmupExplicit = false;
    bool outputExplicit = false;
    bool referenceExplicit = false;
    std::string referencePath;
    std::string verifyPath;
    std::string outputPath;
    std::string obstacle = "none";  // benchmark mode only: none | sphere | blanket
    bool obstacleExplicit = false;
    std::vector<int> gridSizes{32, 64, 96, 128};
    int steps = 100;
    int warmup = 10;
};

bool parsePositiveInt(std::string_view text, int& value, bool allowZero = false) {
    int parsed = 0;
    if (text.empty()) return false;
    for (const char digit : text) {
        if (digit < '0' || digit > '9') return false;
        parsed = parsed * 10 + (digit - '0');
        if (parsed > 4096) return false;
    }
    if (parsed < (allowZero ? 0 : 1)) {
        return false;
    }
    value = parsed;
    return true;
}

void printUsage(std::ostream& out) {
    out << "Usage: smoke_bench [--grid N] [--steps N] [--warmup N] [--output FILE]\n"
           "                   [--obstacle none|sphere|blanket]\n"
           "       smoke_bench --calibrate\n"
           "       smoke_bench --hwinfo\n"
           "       smoke_bench --convergence [--k-all-steps] [--grid N] [--steps N]\n"
           "       smoke_bench --reference FILE [--grid N] [--steps N]\n"
           "       smoke_bench --verify FILE [--grid N] [--steps N]\n";
}

bool parseArgs(int argc, char** argv, Options& options) {
    bool selectedGrid = false;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if (arg == "--calibrate") {
            if (options.calibrate) return false;
            options.calibrate = true;
        } else if (arg == "--hwinfo") {
            if (options.hwinfo) return false;
            options.hwinfo = true;
        } else if (arg == "--convergence") {
            if (options.convergence) return false;
            options.convergence = true;
        } else if (arg == "--k-all-steps") {
            if (options.convergenceAllSteps) return false;
            options.convergenceAllSteps = true;
        } else if (arg == "--grid" || arg == "--steps" || arg == "--warmup" ||
                   arg == "--output" || arg == "--reference" || arg == "--verify" ||
                   arg == "--obstacle") {
            if (i + 1 >= argc) return false;
            const std::string value(argv[++i]);
            if (arg == "--grid") {
                int n = 0;
                if (!parsePositiveInt(value, n)) return false;
                if (!selectedGrid) {
                    options.gridSizes.clear();
                    selectedGrid = true;
                }
                options.gridExplicit = true;
                options.gridSizes.push_back(n);
            } else if (arg == "--steps") {
                if (options.stepsExplicit) return false;
                if (!parsePositiveInt(value, options.steps)) return false;
                options.stepsExplicit = true;
            } else if (arg == "--warmup") {
                if (options.warmupExplicit) return false;
                if (!parsePositiveInt(value, options.warmup, true)) return false;
                options.warmupExplicit = true;
            } else if (arg == "--output") {
                if (options.outputExplicit) return false;
                options.outputPath = value;
                if (options.outputPath.empty()) return false;
                options.outputExplicit = true;
            } else if (arg == "--obstacle") {
                if (options.obstacleExplicit) return false;
                if (value != "none" && value != "sphere" && value != "blanket") return false;
                options.obstacle = value;
                options.obstacleExplicit = true;
            } else if (arg == "--verify") {
                if (!options.verifyPath.empty() || value.empty()) return false;
                options.verifyPath = value;
            } else {
                if (options.referenceExplicit) return false;
                options.referencePath = value;
                if (options.referencePath.empty()) return false;
                options.referenceExplicit = true;
            }
        } else if (arg == "--help" || arg == "-h") {
            printUsage(std::cout);
            std::exit(0);
        } else {
            return false;
        }
    }
    if (options.convergenceAllSteps && !options.convergence) return false;
    // Obstacles are only for the timing mode: the reference scenario has none (VERIFICATION.md, section 3).
    if (options.obstacleExplicit && (options.convergence || options.calibrate || options.hwinfo ||
                                     !options.referencePath.empty() || !options.verifyPath.empty()))
        return false;
    if (options.convergence)
        return !options.calibrate && !options.hwinfo && options.referencePath.empty() &&
               options.verifyPath.empty() && options.outputPath.empty() && !options.warmupExplicit;
    if (options.calibrate && options.hwinfo) return false;
    if (options.calibrate || options.hwinfo)
        return options.referencePath.empty() && options.verifyPath.empty() && options.outputPath.empty() &&
               !options.gridExplicit && !options.stepsExplicit && !options.warmupExplicit;
    if (!options.referencePath.empty() && !options.verifyPath.empty()) return false;
    if (!options.referencePath.empty() || !options.verifyPath.empty())
        return options.outputPath.empty() && !options.warmupExplicit;
    return true;
}

double median(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    const std::size_t middle = values.size() / 2;
    return values.size() % 2 == 0 ? 0.5 * (values[middle - 1] + values[middle]) : values[middle];
}

const char* stageLabel(smoke::Stage stage) {
    switch (stage) {
        case smoke::Stage::Sources:        return "sources";
        case smoke::Stage::Forces:         return "forces";
        case smoke::Stage::AdvectVelocity: return "advect_velocity";
        case smoke::Stage::Project:        return "projection";
        case smoke::Stage::AdvectScalars:  return "advect_scalars";
        default:                           return "unknown";
    }
}

std::string osName() {
#if defined(_WIN32)
    return "Windows";
#elif defined(__APPLE__)
    return "macOS";
#elif defined(__linux__)
    return "Linux";
#else
    return "unknown";
#endif
}

std::string processorName() {
#if defined(_WIN32)
    const char* name = std::getenv("PROCESSOR_IDENTIFIER");
    return name ? name : "unknown";
#elif defined(__APPLE__)
    char name[256]{};
    std::size_t size = sizeof(name);
    return sysctlbyname("machdep.cpu.brand_string", name, &size, nullptr, 0) == 0 ? name : "unknown";
#elif defined(__linux__)
    std::ifstream cpuInfo("/proc/cpuinfo");
    std::string line;
    while (std::getline(cpuInfo, line)) {
        const std::string prefix = "model name";
        if (line.compare(0, prefix.size(), prefix) == 0) {
            const std::size_t colon = line.find(':');
            return colon == std::string::npos ? line : line.substr(colon + 2);
        }
    }
    return "unknown";
#else
    return "unknown";
#endif
}

std::uint64_t memoryBytes() {
#if defined(_WIN32)
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    return GlobalMemoryStatusEx(&status) ? status.ullTotalPhys : 0;
#elif defined(__APPLE__)
    std::uint64_t bytes = 0;
    std::size_t size = sizeof(bytes);
    return sysctlbyname("hw.memsize", &bytes, &size, nullptr, 0) == 0 ? bytes : 0;
#elif defined(__linux__)
    std::ifstream memoryInfo("/proc/meminfo");
    std::string label, unit;
    std::uint64_t kibibytes = 0;
    while (memoryInfo >> label >> kibibytes >> unit) {
        if (label == "MemTotal:") return kibibytes * 1024;
    }
    return 0;
#else
    return 0;
#endif
}

std::uint64_t cacheBytes(const char* key) {
#if defined(__APPLE__)
    std::uint64_t bytes = 0;
    std::size_t size = sizeof(bytes);
    return sysctlbyname(key, &bytes, &size, nullptr, 0) == 0 ? bytes : 0;
#else
    (void)key;
    return 0;
#endif
}

const char* vectorIsa() {
#if defined(__AVX2__)
    return "AVX2";
#elif defined(__aarch64__) && (defined(__ARM_NEON) || defined(__ARM_NEON__))
    return "ARM_NEON (AVX2 unavailable)";
#else
    return "scalar/unknown";
#endif
}

// ---- --hwinfo: what the processor says about itself (CPUID) ------------------------------------------
//
// CPUID is an x86 instruction: the program puts a "leaf" number in EAX (and sometimes a sub-leaf in ECX)
// and the processor answers in EAX, EBX, ECX and EDX. The leaves used here are documented in the AMD
// "CPUID Specification" and in the Intel SDM, volume 2 (instruction CPUID).

struct CpuidRegs {
    unsigned eax = 0, ebx = 0, ecx = 0, edx = 0;
};

// Returns false when the leaf is above the highest leaf the processor supports (or on a non-x86 CPU).
bool cpuid(unsigned leaf, unsigned subleaf, CpuidRegs& regs) {
#if defined(SMOKE_BENCH_X86) && defined(_MSC_VER)
    int info[4] = {0, 0, 0, 0};
    __cpuid(info, static_cast<int>(leaf & 0x80000000u));
    if (static_cast<unsigned>(info[0]) < leaf) return false;
    __cpuidex(info, static_cast<int>(leaf), static_cast<int>(subleaf));
    regs = {static_cast<unsigned>(info[0]), static_cast<unsigned>(info[1]),
            static_cast<unsigned>(info[2]), static_cast<unsigned>(info[3])};
    return true;
#elif defined(SMOKE_BENCH_X86)
    return __get_cpuid_count(leaf, subleaf, &regs.eax, &regs.ebx, &regs.ecx, &regs.edx) != 0;
#else
    (void)leaf; (void)subleaf; (void)regs;
    return false;
#endif
}

void appendRegister(std::string& text, unsigned value) {
    for (int byte = 0; byte < 4; ++byte) text.push_back(static_cast<char>((value >> (8 * byte)) & 0xffu));
}

// Leaf 0: the vendor string is stored in EBX, EDX, ECX (in that order), e.g. "AuthenticAMD".
std::string cpuVendor() {
    CpuidRegs regs;
    if (!cpuid(0, 0, regs)) return "unavailable";
    std::string vendor;
    appendRegister(vendor, regs.ebx);
    appendRegister(vendor, regs.edx);
    appendRegister(vendor, regs.ecx);
    return vendor;
}

// Leaves 0x80000002 to 0x80000004: 48 characters of the marketing name, e.g. "AMD Ryzen 5 5500".
std::string cpuBrand() {
    std::string brand;
    for (unsigned leaf = 0x80000002u; leaf <= 0x80000004u; ++leaf) {
        CpuidRegs regs;
        if (!cpuid(leaf, 0, regs)) return "unavailable";
        appendRegister(brand, regs.eax);
        appendRegister(brand, regs.ebx);
        appendRegister(brand, regs.ecx);
        appendRegister(brand, regs.edx);
    }
    brand = brand.substr(0, brand.find('\0'));
    const std::size_t first = brand.find_first_not_of(' ');
    const std::size_t last = brand.find_last_not_of(' ');
    return first == std::string::npos ? "unavailable" : brand.substr(first, last - first + 1);
}

struct CacheLevel {
    int level = 0;
    const char* type = "";
    std::uint64_t bytes = 0;
    unsigned lineBytes = 0;
    unsigned ways = 0;
    unsigned sharedByThreads = 0;
};

// Cache sizes: leaf 0x8000001D on AMD, leaf 4 on Intel. Both use the same layout, one sub-leaf per cache:
// size = ways * partitions * line size * sets.
std::vector<CacheLevel> cpuCaches() {
    std::vector<CacheLevel> caches;
    const std::string vendor = cpuVendor();
    const unsigned leaf = (vendor == "AuthenticAMD" || vendor == "HygonGenuine") ? 0x8000001Du : 4u;
    for (unsigned subleaf = 0; subleaf < 16; ++subleaf) {
        CpuidRegs regs;
        if (!cpuid(leaf, subleaf, regs)) break;
        const unsigned type = regs.eax & 0x1fu;
        if (type == 0) break;  // no more caches
        CacheLevel cache;
        cache.level = static_cast<int>((regs.eax >> 5) & 0x7u);
        cache.type = type == 1 ? "data" : (type == 2 ? "instruction" : "unified");
        cache.sharedByThreads = ((regs.eax >> 14) & 0xfffu) + 1;
        cache.lineBytes = (regs.ebx & 0xfffu) + 1;
        const unsigned partitions = ((regs.ebx >> 12) & 0x3ffu) + 1;
        cache.ways = ((regs.ebx >> 22) & 0x3ffu) + 1;
        const std::uint64_t sets = static_cast<std::uint64_t>(regs.ecx) + 1;
        cache.bytes = static_cast<std::uint64_t>(cache.ways) * partitions * cache.lineBytes * sets;
        caches.push_back(cache);
    }
    return caches;
}

// "yes" only if the CPU has the instruction set AND the operating system saves the wider registers.
// GCC and Clang check both in __builtin_cpu_supports; on other compilers only the CPUID bit is read.
const char* cpuFeature(const char* name, unsigned leaf, char reg, int bit) {
#if defined(SMOKE_BENCH_X86) && (defined(__GNUC__) || defined(__clang__))
    (void)leaf; (void)reg; (void)bit;
    __builtin_cpu_init();
    const std::string_view feature(name);
    bool supported = false;
    if (feature == "sse4.2") supported = __builtin_cpu_supports("sse4.2");
    else if (feature == "avx") supported = __builtin_cpu_supports("avx");
    else if (feature == "avx2") supported = __builtin_cpu_supports("avx2");
    else if (feature == "fma") supported = __builtin_cpu_supports("fma");
    else if (feature == "avx512f") supported = __builtin_cpu_supports("avx512f");
    return supported ? "yes" : "no";
#else
    (void)name;
    CpuidRegs regs;
    if (!cpuid(leaf, 0, regs)) return "unavailable";
    const unsigned value = reg == 'b' ? regs.ebx : regs.ecx;
    return (value >> bit) & 1u ? "yes (CPUID bit only)" : "no";
#endif
}

void printHardwareInfo() {
    std::cout << "Processor (CPUID)\n"
              << "  vendor: " << cpuVendor() << '\n'
              << "  brand: " << cpuBrand() << '\n';
    CpuidRegs regs;
    if (cpuid(1, 0, regs)) {
        // Leaf 1, EAX: stepping [3:0], model [7:4], family [11:8], extended model [19:16],
        // extended family [27:20]. The extended fields are added only for family 15 (and model for 6 or 15).
        const unsigned baseFamily = (regs.eax >> 8) & 0xfu;
        const unsigned family = baseFamily + (baseFamily == 15 ? (regs.eax >> 20) & 0xffu : 0u);
        unsigned model = (regs.eax >> 4) & 0xfu;
        if (baseFamily == 6 || baseFamily == 15) model |= ((regs.eax >> 16) & 0xfu) << 4;
        std::cout << "  family: " << family << " (0x" << std::hex << family << std::dec << "), model: " << model
                  << " (0x" << std::hex << model << std::dec << "), stepping: " << (regs.eax & 0xfu) << '\n';
    } else {
        std::cout << "  family/model/stepping: unavailable\n";
    }
    std::cout << "  hardware_threads: " << std::thread::hardware_concurrency() << '\n'
              << "  SSE4.2: " << cpuFeature("sse4.2", 1, 'c', 20) << '\n'
              << "  AVX: " << cpuFeature("avx", 1, 'c', 28) << '\n'
              << "  AVX2: " << cpuFeature("avx2", 7, 'b', 5) << '\n'
              << "  FMA: " << cpuFeature("fma", 1, 'c', 12) << '\n'
              << "  AVX-512F: " << cpuFeature("avx512f", 7, 'b', 16) << '\n';

    std::cout << "Caches (CPUID)\n";
    const std::vector<CacheLevel> caches = cpuCaches();
    if (caches.empty()) std::cout << "  unavailable\n";
    for (const CacheLevel& cache : caches)
        std::cout << "  L" << cache.level << ' ' << cache.type << ": " << cache.bytes / 1024 << " KiB, "
                  << cache.ways << "-way, line " << cache.lineBytes << " B, shared by "
                  << cache.sharedByThreads << " hardware thread(s)\n";

    std::cout << "Memory\n  RAM_bytes: ";
    const std::uint64_t ram = memoryBytes();
    if (ram) std::cout << ram << " (" << std::fixed << std::setprecision(1)
                       << static_cast<double>(ram) / (1024.0 * 1024.0 * 1024.0) << " GiB)\n"
                       << std::defaultfloat << std::setprecision(6);
    else std::cout << "unavailable\n";
    std::cout << "  type and speed: not visible to a program; on Windows use\n"
                 "    Get-CimInstance Win32_PhysicalMemory | Select Speed,ConfiguredClockSpeed\n";

    std::cout << "Build\n"
              << "  OS: " << osName() << '\n'
              << "  compiler: " << SMOKE_BENCH_COMPILER_ID << ' ' << SMOKE_BENCH_COMPILER_VERSION << '\n'
              << "  build_type: " << SMOKE_BENCH_BUILD_TYPE << '\n'
              << "  variant: " << SMOKE_BENCH_VARIANT << '\n'
              << "  flags: " << SMOKE_BENCH_FLAGS << '\n'
              << "  per_file_flags: " << SMOKE_BENCH_PER_FILE_FLAGS << '\n'
              << "  avx2_kernels: " << SMOKE_BENCH_AVX2_KERNELS << '\n'
              << "  OpenMP: not enabled (serial-only build)\n"
              << "  commit: " << SMOKE_BENCH_COMMIT << '\n';
}

struct Calibration {
    double memoryGBs = 0.0;
    double dependentNsPerOp = 0.0;
};

// Creates the parent folders of filePath. The error_code overload reports failure instead of throwing.
bool createParentDirectories(const std::string& filePath) {
    const std::filesystem::path parent = std::filesystem::path(filePath).parent_path();
    if (parent.empty()) return true;
    std::error_code error;
    std::filesystem::create_directories(parent, error);
    if (error || !std::filesystem::is_directory(parent, error)) {
        std::cerr << "Cannot create output directory " << parent.string() << '\n';
        return false;
    }
    return true;
}

// One triad pass (the STREAM "triad" kernel): dst[i] = x[i] + scale * y[i].
// It is kept out of line on purpose: every pass is then a real call in the binary, which can be checked
// with "objdump -d smoke_bench.exe" (look for triadPass and the calls to it).
SMOKE_BENCH_NOINLINE void triadPass(float* SMOKE_BENCH_RESTRICT dst, const float* x, const float* y,
                                    float scale, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) dst[i] = x[i] + scale * y[i];
}

// Memory throughput of this computer, used as the denominator of the memory efficiency E
// (docs/plan/OPTIMIZATION.md, section 5.1).
//
// The earlier version ran "a = b + s*c" six times with unchanged b and c. All six passes stored the same
// values, so the compiler legally kept one pass and the result was six times too high (found 2026-11-11).
// Now the three arrays rotate roles: each pass reads the array written by the previous pass, so no pass
// can be dropped.
//
// Each array is 64 MiB (4x the 16 MiB L3 of PC-2), so the data cannot stay in the cache.
// The returned number follows the STREAM convention: 2 reads + 1 write per element. With ordinary
// (not non-temporal) stores the CPU also reads the destination line before writing it ("write allocate"),
// so the real traffic on the memory bus is 4/3 of the returned value.
double measureMemoryTriad() {
    constexpr std::size_t count = 16u * 1024u * 1024u;
    constexpr int passes = 6;
    constexpr float scale = 0.5f;  // values grow by about 1.28x per pass; 42 passes stay far below FLT_MAX
    std::vector<float> a(count, 0.0f), b(count, 1.0f), c(count, 2.0f);
    float* dst = a.data();
    float* x = b.data();
    float* y = c.data();
    auto run = [&] {
        const auto start = Clock::now();
        for (int pass = 0; pass < passes; ++pass) {
            triadPass(dst, x, y, scale, count);
            float* const written = dst;  // rotate: the array just written becomes an input of the next pass
            dst = x;
            x = y;
            y = written;
        }
        const double seconds = std::chrono::duration<double>(Clock::now() - start).count();
        const double bytes = static_cast<double>(count) * 3.0 * sizeof(float) * passes;
        return bytes / seconds / 1.0e9;
    };
    (void)run();
    std::vector<double> samples;
    for (int repeat = 0; repeat < 5; ++repeat) samples.push_back(run());
    volatile float consumed = a.front() + b[count / 2] + c.back();
    (void)consumed;
    return median(samples);
}

double measureDependentChain() {
    constexpr std::uint64_t operations = 20'000'000;
    double value = 0.125;
    const auto start = Clock::now();
    for (std::uint64_t i = 0; i < operations; ++i)
        value = value * 1.0000001 + 0.0000001;
    const double seconds = std::chrono::duration<double>(Clock::now() - start).count();
    volatile double consumed = value;
    (void)consumed;
    return seconds * 1.0e9 / static_cast<double>(operations);
}

void printCalibration() {
    std::vector<double> dependentSamples;
    for (int repeat = 0; repeat < 3; ++repeat) dependentSamples.push_back(measureDependentChain());
    const Calibration calibration{measureMemoryTriad(), median(dependentSamples)};
    const std::uint64_t l2Bytes = cacheBytes("hw.l2cachesize");
    const std::uint64_t l3Bytes = cacheBytes("hw.l3cachesize");
    std::cout << "Machine fingerprint\n"
              << "  OS: " << osName() << '\n'
              << "  CPU: " << processorName() << '\n'
              << "  hardware_threads: " << std::thread::hardware_concurrency() << '\n'
              << "  RAM_bytes: ";
    const std::uint64_t ram = memoryBytes();
    if (ram) std::cout << ram;
    else std::cout << "unavailable";
    std::cout << "\n  L2_cache_bytes: ";
    if (l2Bytes) std::cout << l2Bytes;
    else std::cout << "unavailable";
    std::cout << "\n  L3_cache_bytes: ";
    if (l3Bytes) std::cout << l3Bytes;
    else std::cout << "unavailable";
    std::cout << '\n'
              << "  vector_isa: " << vectorIsa() << '\n'
              << "  OpenMP: not enabled (serial-only build)\n"
              << "  compiler: " << SMOKE_BENCH_COMPILER_ID << ' ' << SMOKE_BENCH_COMPILER_VERSION << '\n'
              << "  build_type: " << SMOKE_BENCH_BUILD_TYPE << '\n'
              << "  variant: " << SMOKE_BENCH_VARIANT << '\n'
              << "  flags: " << SMOKE_BENCH_FLAGS << '\n'
              << "  commit: " << SMOKE_BENCH_COMMIT << '\n'
              << std::fixed << std::setprecision(3)
              << "Calibration\n"
              << "  memory_triad_GB_s: " << calibration.memoryGBs << "  (STREAM convention: 2 reads + 1 write)\n"
              << "  memory_triad_bus_GB_s: " << calibration.memoryGBs * 4.0 / 3.0
              << "  (with write allocate: 3 reads + 1 write)\n"
              << "  dependent_chain_ns_per_op: " << calibration.dependentNsPerOp << '\n';
}

bool writeReference(const std::string& path, const smoke::SmokeSolver& solver, int steps,
                    const smoke::Diagnostics& diagnostics) {
    static_assert(sizeof(float) == sizeof(std::uint32_t) && std::numeric_limits<float>::is_iec559);
    const smoke::DensityView density = solver.density();
    if (!createParentDirectories(path)) return false;

    std::ofstream binary(path, std::ios::binary);
    if (!binary) {
        std::cerr << "Cannot open reference file for writing: " << path << '\n';
        return false;
    }
    std::uint64_t hash = 14695981039346656037ull;
    double sum = 0.0;
    float minimum = std::numeric_limits<float>::infinity();
    float maximum = -std::numeric_limits<float>::infinity();
    for (int k = 0; k < density.nz; ++k)
        for (int j = 0; j < density.ny; ++j)
            for (int i = 0; i < density.nx; ++i) {
                const float value = density.origin[i + density.strideJ * j + density.strideK * k];
                std::uint32_t bits;
                std::memcpy(&bits, &value, sizeof(bits));
                for (int byte = 0; byte < 4; ++byte) {
                    const auto octet = static_cast<unsigned char>((bits >> (8 * byte)) & 0xffu);
                    binary.put(static_cast<char>(octet));
                    hash = (hash ^ octet) * 1099511628211ull;
                }
                sum += value;
                minimum = std::min(minimum, value);
                maximum = std::max(maximum, value);
            }
    binary.close();
    if (!binary) {
        std::cerr << "Failed while writing reference file: " << path << '\n';
        return false;
    }

    std::string summaryPath = path;
    summaryPath += ".txt";
    std::ofstream summary(summaryPath);
    if (!summary) {
        std::cerr << "Cannot open reference summary for writing: " << summaryPath << '\n';
        return false;
    }
    const std::uint64_t cells = static_cast<std::uint64_t>(density.nx) * density.ny * density.nz;
    summary << "format: raw IEEE-754 float32, little-endian, interior cells in z/y/x order\n"
            << "grid: " << density.nx << 'x' << density.ny << 'x' << density.nz << '\n'
            << "steps: " << steps << '\n'
            << "build_type: " << SMOKE_BENCH_BUILD_TYPE << '\n'
            << "compiler: " << SMOKE_BENCH_COMPILER_ID << ' ' << SMOKE_BENCH_COMPILER_VERSION << '\n'
            << "variant: " << SMOKE_BENCH_VARIANT << '\n'
            << "flags: " << SMOKE_BENCH_FLAGS << '\n'
            << "commit: " << SMOKE_BENCH_COMMIT << '\n'
            << "dt: " << solver.params().dt << '\n'
            << "jacobi_iterations: " << solver.params().jacobiIterations << '\n'
            << "buoyancy_alpha: " << solver.params().buoyancyAlpha << '\n'
            << "buoyancy_beta: " << solver.params().buoyancyBeta << '\n'
            << "ambient_temperature: " << solver.params().ambientTemp << '\n'
            << "vorticity_epsilon: " << solver.params().vorticityEps << '\n'
            << "source: (" << solver.params().source.x << ", " << solver.params().source.y << ", "
            << solver.params().source.z << "), radius=" << solver.params().source.radius
            << ", density_rate=" << solver.params().source.densityRate
            << ", temperature_rate=" << solver.params().source.temperatureRate << '\n'
            << std::hex << std::setfill('0') << "fnv1a64: " << std::setw(16) << hash << std::dec << '\n'
            << std::setprecision(9) << "min: " << minimum << "\nmax: " << maximum
            << "\nmean: " << sum / static_cast<double>(cells)
            << "\nmax_div_after: " << diagnostics.maxDivAfter
            << "\nnon_finite_cells: " << diagnostics.nonFiniteCells << '\n';
    summary.close();
    if (!summary) {
        std::cerr << "Failed while writing reference summary: " << summaryPath << '\n';
        return false;
    }
    std::cout << "Reference written: " << path << " (" << cells * sizeof(float)
              << " bytes), summary: " << summaryPath << '\n';
    return true;
}

bool runReference(const Options& options) {
    smoke::SolverConfig config;
    const int n = options.gridExplicit ? options.gridSizes.front() : 64;
    config.grid = {n, n, n};
    if (options.gridExplicit && options.gridSizes.size() != 1) {
        std::cerr << "--reference accepts exactly one --grid size\n";
        return false;
    }
    smoke::SmokeSolver solver(config);
    for (int step = 0; step < options.steps; ++step) solver.step();
    const smoke::Diagnostics diagnostics = solver.diagnostics();
    if (diagnostics.nonFiniteCells != 0) {
        std::cerr << "Cannot write reference: solver state contains " << diagnostics.nonFiniteCells
                  << " non-finite cells\n";
        return false;
    }
    return writeReference(options.referencePath, solver, options.steps, diagnostics);
}

// Runs the same fixed scenario as --reference and compares the final density with a reference file
// cell by cell (docs/plan/VERIFICATION.md, section 3).
bool runVerify(const Options& options) {
    const int n = options.gridExplicit ? options.gridSizes.front() : 64;
    if (options.gridExplicit && options.gridSizes.size() != 1) {
        std::cerr << "--verify accepts exactly one --grid size\n";
        return false;
    }
    const std::uint64_t cells = static_cast<std::uint64_t>(n) * n * n;

    std::ifstream file(options.verifyPath, std::ios::binary);
    if (!file) {
        std::cerr << "Cannot open reference file: " << options.verifyPath << '\n';
        return false;
    }
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (bytes.size() != cells * sizeof(float)) {
        std::cerr << "Reference file has " << bytes.size() << " bytes, expected " << cells * sizeof(float)
                  << " for " << n << "^3 (use --grid N to match the reference)\n";
        return false;
    }

    smoke::SolverConfig config;
    config.grid = {n, n, n};
    smoke::SmokeSolver solver(config);
    for (int step = 0; step < options.steps; ++step) solver.step();
    const smoke::Diagnostics diagnostics = solver.diagnostics();
    if (diagnostics.nonFiniteCells != 0) {
        std::cerr << "Verification failed: solver state contains " << diagnostics.nonFiniteCells
                  << " non-finite cells\n";
        return false;
    }
    const smoke::DensityView density = solver.density();

    double maxAbs = 0.0, maxRef = 0.0;
    std::uint64_t different = 0, index = 0;
    int worstI = 0, worstJ = 0, worstK = 0;
    for (int k = 0; k < density.nz; ++k)
        for (int j = 0; j < density.ny; ++j)
            for (int i = 0; i < density.nx; ++i, ++index) {
                std::uint32_t bits = 0;  // the file is little-endian, as written by writeReference
                for (int byte = 0; byte < 4; ++byte)
                    bits |= static_cast<std::uint32_t>(bytes[index * 4 + byte]) << (8 * byte);
                float reference;
                std::memcpy(&reference, &bits, sizeof(reference));
                const float value = density.origin[i + density.strideJ * j + density.strideK * k];
                const double diff = std::fabs(static_cast<double>(value) - reference);
                if (value != reference) ++different;
                if (diff > maxAbs) { maxAbs = diff; worstI = i + 1; worstJ = j + 1; worstK = k + 1; }
                maxRef = std::max(maxRef, std::fabs(static_cast<double>(reference)));
            }

    const double relativeError = maxRef > 0.0 ? maxAbs / maxRef
                                              : (maxAbs == 0.0 ? 0.0 : std::numeric_limits<double>::infinity());
    const bool passed = relativeError <= kVerifyRelativeTolerance;
    std::cout << "Verify against " << options.verifyPath << " (" << n << "^3, " << options.steps << " steps)\n"
              << std::setprecision(6)
              << "  cells_different: " << different << " of " << cells << '\n'
              << "  max_abs_diff: " << maxAbs << " at cell (" << worstI << ", " << worstJ << ", " << worstK << ")\n"
              << "  max_abs_diff / max|reference|: " << relativeError << '\n'
              << "  relative_tolerance: " << kVerifyRelativeTolerance << '\n'
              << "  bit_identical: " << (different == 0 ? "yes" : "no") << '\n';
    std::cout << "  result: " << (passed ? "PASS" : "FAIL") << '\n';
    return passed;
}

// ---- --convergence: how far does K Jacobi iterations get? (docs/plan/EXECUTION_PLAN.md, P2) -----------
//
// This mode needs the pressure and divergence fields, which the public SmokeSolver interface does not
// expose, so it drives the internal stage functions directly (as the tests, smoke_lab and smoke_prof do).

// One whole step: the same calls in the same order as SmokeSolver::step (Solver/src/SmokeSolver.cpp).
void convergenceStep(smoke::SmokeState& state, const smoke::SmokeParams& params, smoke::serial::SlabRange all) {
    using namespace smoke::serial;
    addSources(state, params, params.dt, all);
    applyForces(state, params, params.dt, all);
    advectVelocity(state, params.dt, all);
    project(state, params, all);
    advectScalars(state, params.dt, all);
}

struct ConvergenceRow {
    double maxDivBefore = 0.0;   // max |div u| before the projection of the last step
    double maxDivAfter = 0.0;    // max |div u| after it
    double rmsDivAfter = 0.0;    // root mean square of div u after it
    double poissonResidual = 0.0;  // max | laplacian(p) - div |: how well the Poisson equation is solved
};

// Must be called right after a whole step: state.divergence then still holds the divergence BEFORE the
// correction (see project() in Stages.h), state.pressure the final pressure with an up-to-date ghost layer,
// and u, v, w the corrected velocity (advectScalars does not change them).
ConvergenceRow measureConvergence(const smoke::SmokeState& s) {
    ConvergenceRow row;
    const smoke::GridSize g = s.density.size();
    double sumSquares = 0.0;
    for (int k = 1; k <= g.nz; ++k)
        for (int j = 1; j <= g.ny; ++j)
            for (int i = 1; i <= g.nx; ++i) {
                const double divBefore = s.divergence.at(i, j, k);
                // Same formula as computeDivergence (stages/Projection.cpp), in double.
                const double divAfter = 0.5 * (double(s.u.at(i + 1, j, k)) - s.u.at(i - 1, j, k) +
                                               double(s.v.at(i, j + 1, k)) - s.v.at(i, j - 1, k) +
                                               double(s.w.at(i, j, k + 1)) - s.w.at(i, j, k - 1));
                // Seven-point Laplacian with h = 1, the operator the Jacobi iteration solves
                // (docs/plan/MATH.md, section 5): sum of the 6 neighbours - 6 p.
                const double laplacian = double(s.pressure.at(i - 1, j, k)) + s.pressure.at(i + 1, j, k) +
                                         double(s.pressure.at(i, j - 1, k)) + s.pressure.at(i, j + 1, k) +
                                         double(s.pressure.at(i, j, k - 1)) + s.pressure.at(i, j, k + 1) -
                                         6.0 * double(s.pressure.at(i, j, k));
                row.maxDivBefore = std::max(row.maxDivBefore, std::fabs(divBefore));
                row.maxDivAfter = std::max(row.maxDivAfter, std::fabs(divAfter));
                row.poissonResidual = std::max(row.poissonResidual, std::fabs(laplacian - divBefore));
                sumSquares += divAfter * divAfter;
            }
    row.rmsDivAfter = std::sqrt(sumSquares / (static_cast<double>(g.nx) * g.ny * g.nz));
    return row;
}

// Two ways of using K (column "mode"):
//   last_step  steps-1 steps with the default K, then ONE step with K. Every K gets exactly the same input
//              (same divergence before), so the rows of one grid are directly comparable. Default.
//   all_steps  every step uses K (--k-all-steps): what a whole simulation run with that K looks like.
bool runConvergence(const Options& options) {
    const std::vector<int> grids = options.gridExplicit ? options.gridSizes : std::vector<int>{32, 64, 128};
    constexpr std::array<int, 6> iterationCounts{10, 20, 40, 80, 160, 1000};
    constexpr std::array<float, 2> epsilons{5.0f, 0.0f};
    std::cout << "grid,vorticity_eps,mode,steps,jacobi_iters,max_div_before,max_div_after,ratio_after_before,"
                 "rms_div_after,poisson_residual_max\n";
    for (const int n : grids) {
        for (const float epsilon : epsilons) {
            smoke::SolverConfig config;
            config.grid = {n, n, n};
            config.params.vorticityEps = epsilon;
            const smoke::serial::SlabRange all = smoke::serial::wholeInterior(config.grid);

            // last_step mode: develop the flow once, remember all fields, and replay the last step for each K.
            smoke::SmokeState state;
            state.resize(config.grid);
            std::vector<std::vector<float>> saved;
            if (!options.convergenceAllSteps) {
                for (int step = 0; step + 1 < options.steps; ++step) convergenceStep(state, config.params, all);
                for (smoke::Field3D* field : state.fields())
                    saved.emplace_back(field->data(), field->data() + field->totalFloats());
            }

            for (const int iterations : iterationCounts) {
                smoke::SmokeParams params = config.params;
                params.jacobiIterations = iterations;
                if (options.convergenceAllSteps) {
                    state.clear();
                    for (int step = 0; step < options.steps; ++step) convergenceStep(state, params, all);
                } else {
                    std::size_t index = 0;
                    for (smoke::Field3D* field : state.fields()) {
                        std::memcpy(field->data(), saved[index].data(), saved[index].size() * sizeof(float));
                        ++index;
                    }
                    convergenceStep(state, params, all);
                }
                const ConvergenceRow row = measureConvergence(state);
                if (!std::isfinite(row.maxDivAfter) || !std::isfinite(row.poissonResidual)) {
                    std::cerr << "Non-finite state at grid " << n << "^3, K = " << iterations << '\n';
                    return false;
                }
                std::cout << n << "^3," << epsilon << ','
                          << (options.convergenceAllSteps ? "all_steps" : "last_step") << ',' << options.steps << ','
                          << iterations << ',' << std::setprecision(6) << row.maxDivBefore << ',' << row.maxDivAfter
                          << ',' << (row.maxDivBefore > 0.0 ? row.maxDivAfter / row.maxDivBefore : 0.0) << ','
                          << row.rmsDivAfter << ',' << row.poissonResidual << std::endl;
            }
        }
    }
    return true;
}

// Obstacle scenarios for measuring what obstacles cost per step (docs/plan/EXECUTION_PLAN.md, P4.4).
// The cells are marked directly from a formula, without a mesh, so the scenario is the same on every run
// and scales with the grid. Positions follow the renderer demo (Integration/src/SmokeScene.cpp):
//   sphere   solid ball, centre (0.5, 0.5, 0.5), radius 0.15 of the box, above the smoke source;
//   blanket  porous horizontal layer two cells thick at height 0.45, 0.7 x 0.7 of the box, sigma = 20 1/s.
// A cell belongs to the obstacle when its centre does. Returns an all-Fluid grid for "none".
smoke::ObstacleGrid makeBenchObstacles(const std::string& name, const smoke::GridSize& size) {
    smoke::ObstacleGrid grid(size);
    if (name == "sphere") {
        constexpr double radius = 0.15;
        for (int k = 1; k <= size.nz; ++k)
            for (int j = 1; j <= size.ny; ++j)
                for (int i = 1; i <= size.nx; ++i) {
                    const double x = (i - 0.5) / size.nx - 0.5;
                    const double y = (j - 0.5) / size.ny - 0.5;
                    const double z = (k - 0.5) / size.nz - 0.5;
                    if (x * x + y * y + z * z <= radius * radius) grid.set(i, j, k, smoke::CellType::Solid);
                }
    } else if (name == "blanket") {
        grid.porousDrag = 20.0f;  // the default of the renderer panel; a tuning value, not a measured material
        const int firstLayer = static_cast<int>(0.45 * size.ny) + 1;
        for (int k = 1; k <= size.nz; ++k)
            for (int j = firstLayer; j <= std::min(firstLayer + 1, size.ny); ++j)
                for (int i = 1; i <= size.nx; ++i) {
                    const double x = (i - 0.5) / size.nx - 0.5;
                    const double z = (k - 0.5) / size.nz - 0.5;
                    if (std::fabs(x) <= 0.35 && std::fabs(z) <= 0.35) grid.set(i, j, k, smoke::CellType::Porous);
                }
    }
    return grid;
}

bool runBenchmarks(const Options& options, std::ostream& csv) {
    csv << "grid,stage,median_ms,min_ms,max_ms,steps\n";
    for (const int n : options.gridSizes) {
        smoke::SolverConfig config;
        config.grid = {n, n, n};
        smoke::SmokeSolver solver(config);
        if (options.obstacle != "none") {
            const smoke::ObstacleGrid obstacles = makeBenchObstacles(options.obstacle, config.grid);
            if (!solver.setObstacles(obstacles)) {
                std::cerr << "Cannot set obstacles at grid " << n << "^3\n";
                return false;
            }
            // List sizes go to stderr, so the CSV on stdout keeps its format.
            smoke::SmokeState layout;
            layout.resize(config.grid);
            const smoke::ObstacleLists lists = smoke::buildObstacleLists(obstacles, layout.density);
            std::cerr << "obstacle=" << options.obstacle << " grid=" << n << "^3 solid=" << lists.solid.size()
                      << " boundary_solid=" << lists.boundarySolid.size() << " neighbour_links="
                      << lists.neighbour.size() << " porous=" << lists.porous.size() << '\n';
        }
        for (int step = 0; step < options.warmup; ++step) solver.step();

        std::array<std::vector<double>, smoke::kStageCount + 2> samples;
        for (int step = 0; step < options.steps; ++step) {
            solver.step();
            const smoke::StepTimings& timing = solver.lastTimings();
            for (std::size_t stage = 0; stage < smoke::kStageCount; ++stage)
                samples[stage].push_back(timing.ms[stage]);
            samples[smoke::kStageCount].push_back(timing.jacobiIterationMs);
            samples[smoke::kStageCount + 1].push_back(timing.total());
        }

        auto emit = [&](const char* stage, const std::vector<double>& values) {
            const double minimum = *std::min_element(values.begin(), values.end());
            const double maximum = *std::max_element(values.begin(), values.end());
            csv << n << "^3," << stage << ',' << std::setprecision(9) << median(values) << ','
                << minimum << ',' << maximum << ',' << options.steps << '\n';
        };
        for (std::size_t stage = 0; stage < smoke::kStageCount; ++stage)
            emit(stageLabel(static_cast<smoke::Stage>(stage)), samples[stage]);
        emit("jacobi_iteration", samples[smoke::kStageCount]);
        emit("total", samples[smoke::kStageCount + 1]);

        if (solver.diagnostics().nonFiniteCells != 0) {
            std::cerr << "Non-finite solver state at grid " << n << "^3\n";
            return false;
        }
    }
    return static_cast<bool>(csv);
}

} // namespace

int main(int argc, char** argv) {
    Options options;
    if (!parseArgs(argc, argv, options)) {
        printUsage(std::cerr);
        return 2;
    }
    if (options.calibrate) {
        printCalibration();
        return 0;
    }
    if (options.hwinfo) {
        printHardwareInfo();
        return 0;
    }
    if (options.convergence) return runConvergence(options) ? 0 : 1;
    if (!options.referencePath.empty()) return runReference(options) ? 0 : 1;
    if (!options.verifyPath.empty()) return runVerify(options) ? 0 : 1;

    if (!options.outputPath.empty()) {
        if (!createParentDirectories(options.outputPath)) return 1;
        std::ofstream output(options.outputPath);
        if (!output) {
            std::cerr << "Cannot open CSV output file: " << options.outputPath << '\n';
            return 1;
        }
        return runBenchmarks(options, output) ? 0 : 1;
    }
    return runBenchmarks(options, std::cout) ? 0 : 1;
}
