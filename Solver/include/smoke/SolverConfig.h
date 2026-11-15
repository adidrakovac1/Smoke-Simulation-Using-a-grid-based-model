#pragma once

#include "smoke/GridSize.h"
#include "smoke/SmokeParams.h"

namespace smoke {

/// Which implementation of the stages is used:
///   Serial: single-threaded execution (reference)
///   CpuParallel: opt-in multi-threaded CPU parallel execution
enum class Backend { Serial, CpuParallel };

struct SolverConfig {
    GridSize    grid;                      // default 64x64x64
    SmokeParams params;
    Backend     backend = Backend::Serial;
    int         threads = 0;               // 0 = auto (hardware concurrency) when CpuParallel is selected
};

} // namespace smoke
