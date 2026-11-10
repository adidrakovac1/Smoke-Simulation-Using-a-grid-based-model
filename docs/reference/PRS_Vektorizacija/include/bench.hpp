#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>


#if defined(_MSC_VER)
#define PERF_NOINLINE __declspec(noinline)
#else
#define PERF_NOINLINE __attribute__((noinline))
#endif

#if defined(_MSC_VER)
#define PERF_RESTRICT __restrict
#elif defined(__GNUC__) || defined(__clang__)
#define PERF_RESTRICT __restrict__
#else
#define PERF_RESTRICT
#endif

#if defined(__clang__)
#define PERF_NO_VECTORIZE \
        _Pragma("clang loop vectorize(disable) interleave(disable)")
#elif defined(_MSC_VER)
#define PERF_NO_VECTORIZE __pragma(loop(no_vector))
#elif defined(__GNUC__) && __GNUC__ >= 14
#define PERF_NO_VECTORIZE _Pragma("GCC novector")
#else
#define PERF_NO_VECTORIZE
#endif

namespace perf
{
    struct Config
    {
        int warmups = 1;
        int repeats = 10;
        std::size_t n = 1u << 17;
        std::size_t matrix = 128;
        std::size_t max_mb = 128;
    };

    struct Sample
    {
        double median_s = 0;
        double min_s = 0;
        std::vector<double> runs_s;
    };

    struct Row
    {
        std::string name;
        std::string metric;
        double median_ms = 0;
        double min_ms = 0;
        double value = 0;
        std::vector<double> runs_s;
    };

    bool avx2_supported();
    PERF_NOINLINE double checksum(const float* ptr, std::size_t n);
    PERF_NOINLINE double checksum(const std::uint32_t* ptr, std::size_t n);
    PERF_NOINLINE void consume(double result);
    std::uint64_t next_random(std::uint64_t& state);
    void fill_floats(float* ptr, std::size_t n, std::uint64_t seed);

    template <class Run, class Observe>
    Sample measure(const Config& config, Run run, Observe observe)
    {
        using clock = std::chrono::steady_clock;

        if (config.repeats <= 0 || config.warmups < 0)
            throw std::invalid_argument("Invalid benchmark repeat/warmup count");

        for (int i = 0; i < config.warmups; ++i)
        {
            run();
            consume(observe());
        }

        std::vector<double> seconds;
        seconds.reserve(static_cast<std::size_t>(config.repeats));

        for (int i = 0; i < config.repeats; ++i)
        {
            std::atomic_signal_fence(std::memory_order_seq_cst);
            const auto start = clock::now();
            run();
            const auto end = clock::now();
            std::atomic_signal_fence(std::memory_order_seq_cst);

            consume(observe());
            seconds.push_back(std::chrono::duration<double>(end - start).count());
        }

        std::vector<double> sorted = seconds;
        std::sort(sorted.begin(), sorted.end());
        const std::size_t mid = sorted.size() / 2;
        const double median = (sorted.size() % 2) ? sorted[mid] : (sorted[mid - 1] + sorted[mid]) / 2.0;

        return { median, sorted.front(), std::move(seconds) };
    }

    class Report
    {
    private:
        Config _config;
        std::vector<Row> _rows;

    public:
        explicit Report(Config config);

        void add(const std::string& name, const Sample& sample,
                 double metric_value, std::string unit);
        void print_summary() const;
    };

    void particle_update(const Config&, Report&);
}
