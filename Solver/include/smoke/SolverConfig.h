#pragma once

#include "smoke/GridSize.h"
#include "smoke/SmokeParams.h"

namespace smoke {

/// Which implementation of the stages is used:
///   Serial: single-threaded execution (reference)
///   CpuParallel: multi-threaded CPU parallel execution (default for optimized solver)
enum class Backend { Serial, CpuParallel };

struct SolverConfig {
    GridSize    grid;                      // default 64x64x64
    SmokeParams params;
    Backend     backend = Backend::CpuParallel;
    int         threads = 0;               // 0 = auto (hardware concurrency), 1 = single-threaded
};

} // namespace smoke
