#pragma once

#include "smoke/GridSize.h"
#include "smoke/SmokeParams.h"

namespace smoke {

/// Which implementation of the stages is used:
///   Serial: single-threaded execution (reference)
///   CpuParallel: multi-threaded CPU execution (D18, D19); bit-identical to Serial for any thread count
enum class Backend { Serial, CpuParallel };

struct SolverConfig {
    GridSize    grid;                      // default 64x64x64
    SmokeParams params;
    Backend     backend = Backend::Serial;
    int         threads = 0;               // 0 = auto (one per physical core) when CpuParallel is selected
    /// Jacobi iterations per wave of the temporal-blocking schedule (D19, stages/JacobiWavefront.cpp);
    /// 0 = automatic. Changes only the speed, never the result.
    int         jacobiWaveDepth = 0;
    /// Planes each iteration of a wave advances between two barriers (D19); 0 = automatic. Speed only.
    int         jacobiPlanesPerTick = 0;
};

} // namespace smoke
