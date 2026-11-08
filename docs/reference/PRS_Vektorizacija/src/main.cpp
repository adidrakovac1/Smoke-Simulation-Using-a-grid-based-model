#include "benchmark_runner.hpp"

#include <iostream>

int main()
{
    perf::Config config;
    config.n = 1u << 17;        
    config.matrix = 256;        
    config.max_mb = 128;        
    config.repeats = 10;        
    config.warmups = 1;         

  
    perf::BenchmarkRunner runner(config);
    runner.run();

    return 0;
}