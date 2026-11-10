#include "bench.hpp"

#include <atomic>
#include <cstring>
#include <iomanip>
#include <iostream>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace perf
{
    namespace
    {
        std::atomic<std::uint64_t> observed_bits{0};
    }

    bool avx2_supported()
    {
#if !defined(PERFLABS_HAS_AVX2_BINARY)
        return false;
#elif defined(_MSC_VER)
        int cpu[4] = { 0, 0, 0, 0 };
        __cpuid(cpu, 0);
        if (cpu[0] < 7)
            return false;

        __cpuid(cpu, 1);
        if (!(cpu[2] & (1 << 27)) || !(cpu[2] & (1 << 28)))
            return false;

        if ((_xgetbv(0) & 0x6) != 0x6)
            return false;

        __cpuidex(cpu, 7, 0);
        return (cpu[1] & (1 << 5)) != 0;
#elif defined(__i386__) || defined(__x86_64__)
        __builtin_cpu_init();
        return __builtin_cpu_supports("avx2") != 0;
#else
        return false;
#endif
    }

    PERF_NOINLINE double checksum(const float* ptr, std::size_t n)
    {
        double sum = 0;
        for (std::size_t i = 0; i < n; ++i)
            sum += ptr[i];
        return sum;
    }

    PERF_NOINLINE double checksum(const std::uint32_t* ptr, std::size_t n)
    {
        double sum = 0;
        for (std::size_t i = 0; i < n; ++i)
            sum += ptr[i];
        return sum;
    }

    // Ne zelimo da kompajler optimizuje rad
    PERF_NOINLINE void consume(double result)
    {
        std::uint64_t bits = 0;
        static_assert(sizeof(bits) == sizeof(result));
        std::memcpy(&bits, &result, sizeof(bits));
        observed_bits.fetch_xor(bits, std::memory_order_relaxed);
    }

    std::uint64_t next_random(std::uint64_t& state)
    {
        //jednostavan pseudorandom broj
        state = state * 6364136223846793005ULL + 1;
        return state;
    }

    void fill_floats(float* ptr, std::size_t n, std::uint64_t seed)
    {
        for (std::size_t i = 0; i < n; ++i)
            ptr[i] = static_cast<float>(static_cast<int>(next_random(seed) % 2001) - 1000) / 1000.f;
    }

    Report::Report(Config config)
        : _config(std::move(config))
    {
    }

    void Report::add(const std::string& name, const Sample& sample,
                     double metric_value, std::string unit)
    {
        _rows.push_back({ name, std::move(unit),
                          sample.median_s * 1000.0, sample.min_s * 1000.0,
                          metric_value, sample.runs_s });
    }

    void Report::print_summary() const
    {
        std::cout << std::fixed << std::setprecision(4);

        for (const auto& row : _rows)
        {
            std::cout << "\nTest: " << row.name << "\n";
            for (std::size_t i = 0; i < row.runs_s.size(); ++i)
                std::cout << "  Run " << std::setw(2) << (i + 1) << ": "
                          << std::setw(10) << row.runs_s[i] * 1000.0 << " ms\n";

            const double maximum = *std::max_element(row.runs_s.begin(), row.runs_s.end()) * 1000.0;
            std::cout << "  Median:  " << row.median_ms << " ms\n";
            std::cout << "  Minimum: " << row.min_ms << " ms\n";
            std::cout << "  Maximum: " << maximum << " ms\n";
            std::cout << "  Metric:  " << row.value << ' ' << row.metric << "\n";
        }

    }
}
