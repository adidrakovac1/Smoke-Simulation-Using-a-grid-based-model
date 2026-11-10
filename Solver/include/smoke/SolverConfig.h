#pragma once

#include "smoke/GridSize.h"
#include "smoke/SmokeParams.h"

namespace smoke {

/// Which implementation of the stages is used. Only Serial exists in the serial phase;
/// OpenMP / SIMD / GPU variants are added later (docs/plan/OPTIMIZATION.md) while Serial stays
/// as the correctness and speed reference.
enum class Backend { Serial };

struct SolverConfig {
    GridSize    grid;                      // default 64x64x64
    SmokeParams params;
    Backend     backend = Backend::Serial;
};

} // namespace smoke
