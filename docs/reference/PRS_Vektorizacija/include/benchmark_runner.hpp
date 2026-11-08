#pragma once

#include "bench.hpp"


namespace perf
{
    class BenchmarkRunner
    {
    private:
        Config _config;
        Report _report;

        void run_measurement();
        void run_memory();
        void run_vectorization();
        void run_particle_update();

    public:
        explicit BenchmarkRunner(const Config& config);
        void run();
    };
}
