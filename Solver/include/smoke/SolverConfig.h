#pragma once

#include "smoke/GridSize.h"
#include "smoke/SmokeParams.h"

namespace smoke {

/// Which implementation of the stages is used:
///   Serial: single-threaded execution (reference)
///   CpuParallel: multi-threaded CPU execution (D18, D19); bit-identical to Serial for any thread count
enum class Backend { Serial, CpuParallel };

/// How the threads of the CpuParallel backend wait for each other (decision D20):
///   Spin   busy-wait with the CPU "pause" hint (fastest: answers in well under a microsecond); the waiting
///          cycles count as executed instructions, so the measured CPI of the program goes up;
///   Block  sleep in the operating system until woken (C++20 atomic wait). A sleeping thread uses no cycles,
///          so they do not enter the CPI, but every wake-up costs microseconds, so a step is slower.
/// Changes only the speed and the CPI, never the result.
enum class ThreadWait { Spin, Block };

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
    ThreadWait  threadWait = ThreadWait::Spin;  // D20, see above
};

} // namespace smoke
