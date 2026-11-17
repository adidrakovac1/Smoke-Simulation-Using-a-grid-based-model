#pragma once

namespace smoke {

/// True when this binary contains the AVX2 kernels (SMOKE_HAS_AVX2_BINARY, set by CMake for x86 targets)
/// AND the processor and the operating system support AVX2 (lab technique T15, docs/plan/OPTIMIZATION.md;
/// same checks as perf::avx2_supported() in docs/reference/PRS_Vektorizacija/src/bench.cpp).
/// The answer is computed on every call (no cached global state); it costs well under a microsecond and is
/// asked once per kernel call, not once per cell.
bool avx2Supported();

/// Number of physical processor cores (decision D19): on Windows from GetLogicalProcessorInformation (one
/// RelationProcessorCore entry per core), elsewhere std::thread::hardware_concurrency(). Never less than 1.
/// The default team size of the CpuParallel backend (ThreadPool::defaultThreadCount).
int physicalCoreCount();

/// Pins the CALLING thread, member `index` of a team of `teamSize` threads, so that the team is spread evenly
/// over the processor (D19). Team no larger than the number of physical cores: thread i may run on both
/// logical processors of core i. Larger team (SMT): thread i runs only on logical processor (i / cores) of
/// core (i % cores), so no logical processor gets two threads of the team. Windows only; elsewhere it does
/// nothing. Returns true when the affinity was set.
bool pinCurrentThread(int index, int teamSize);

} // namespace smoke
