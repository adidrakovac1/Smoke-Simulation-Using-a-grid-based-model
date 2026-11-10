#include "smoke/SmokeSolver.h"

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

namespace {

using Clock = std::chrono::steady_clock;
constexpr double kVerifyRelativeTolerance = 1.0e-4;

struct Options {
    bool calibrate = false;
    bool gridExplicit = false;
    bool stepsExplicit = false;
    bool warmupExplicit = false;
    bool outputExplicit = false;
    bool referenceExplicit = false;
    std::string referencePath;
    std::string verifyPath;
    std::string outputPath;
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
           "       smoke_bench --calibrate\n"
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
        } else if (arg == "--grid" || arg == "--steps" || arg == "--warmup" ||
                   arg == "--output" || arg == "--reference" || arg == "--verify") {
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
    if (options.calibrate)
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

double measureMemoryTriad() {
    constexpr std::size_t count = 16u * 1024u * 1024u;
    constexpr int passes = 6;
    std::vector<float> a(count, 0.0f), b(count, 1.0f), c(count, 2.0f);
    auto run = [&] {
        const auto start = Clock::now();
        for (int pass = 0; pass < passes; ++pass)
            for (std::size_t i = 0; i < count; ++i) a[i] = b[i] + 1.25f * c[i];
        const double seconds = std::chrono::duration<double>(Clock::now() - start).count();
        const double bytes = static_cast<double>(count) * 3.0 * sizeof(float) * passes;
        return bytes / seconds / 1.0e9;
    };
    (void)run();
    std::vector<double> samples;
    for (int repeat = 0; repeat < 3; ++repeat) samples.push_back(run());
    volatile float consumed = a.front() + a[count / 2] + a.back();
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
              << "  memory_triad_GB_s: " << calibration.memoryGBs << '\n'
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

bool runBenchmarks(const Options& options, std::ostream& csv) {
    csv << "grid,stage,median_ms,min_ms,max_ms,steps\n";
    for (const int n : options.gridSizes) {
        smoke::SolverConfig config;
        config.grid = {n, n, n};
        smoke::SmokeSolver solver(config);
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
