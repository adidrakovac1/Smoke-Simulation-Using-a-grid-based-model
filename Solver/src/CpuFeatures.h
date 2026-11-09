#pragma once

namespace smoke {

/// True when this binary contains the AVX2 kernels (SMOKE_HAS_AVX2_BINARY, set by CMake for x86 targets)
/// AND the processor and the operating system support AVX2 (lab technique T15, docs/plan/OPTIMIZATION.md;
/// same checks as perf::avx2_supported() in docs/reference/PRS_Vektorizacija/src/bench.cpp).
/// The answer is computed on every call (no cached global state); it costs well under a microsecond and is
/// asked once per kernel call, not once per cell.
bool avx2Supported();

} // namespace smoke
