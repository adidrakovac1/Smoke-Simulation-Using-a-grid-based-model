#include "CpuFeatures.h"

#include <thread>
#include <vector>

#if defined(SMOKE_HAS_AVX2_BINARY) && defined(_MSC_VER)
#include <immintrin.h>
#include <intrin.h>
#endif

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace smoke {

int physicalCoreCount() {
    const int logical = static_cast<int>(std::thread::hardware_concurrency());
#if defined(_WIN32)
    DWORD bytes = 0;
    GetLogicalProcessorInformation(nullptr, &bytes);
    if (bytes > 0) {
        std::vector<SYSTEM_LOGICAL_PROCESSOR_INFORMATION> info(bytes / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION));
        if (GetLogicalProcessorInformation(info.data(), &bytes)) {
            int cores = 0;
            for (const auto& entry : info)
                if (entry.Relationship == RelationProcessorCore) ++cores;
            if (cores > 0) return cores;
        }
    }
#endif
    return logical > 0 ? logical : 1;
}

bool pinCurrentThread(int index, int teamSize) {
#if defined(_WIN32)
    DWORD bytes = 0;
    GetLogicalProcessorInformation(nullptr, &bytes);
    if (bytes == 0) return false;
    std::vector<SYSTEM_LOGICAL_PROCESSOR_INFORMATION> info(bytes / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION));
    if (!GetLogicalProcessorInformation(info.data(), &bytes)) return false;
    std::vector<ULONG_PTR> masks;
    for (const auto& entry : info)
        if (entry.Relationship == RelationProcessorCore) masks.push_back(entry.ProcessorMask);
    if (masks.empty() || index < 0) return false;
    const std::size_t cores = masks.size();
    ULONG_PTR mask = masks[static_cast<std::size_t>(index) % cores];
    if (static_cast<std::size_t>(teamSize) > cores) {
        // Keep only logical processor number (index / cores) of this core.
        std::size_t sibling = (static_cast<std::size_t>(index) / cores);
        ULONG_PTR one = 0;
        for (ULONG_PTR bits = mask; bits != 0; bits &= bits - 1) {
            const ULONG_PTR lowest = bits & (~bits + 1);
            if (sibling == 0) {
                one = lowest;
                break;
            }
            --sibling;
        }
        if (one != 0) mask = one;
    }
    return SetThreadAffinityMask(GetCurrentThread(), mask) != 0;
#else
    (void)index;
    (void)teamSize;
    return false;
#endif
}

bool avx2Supported() {
#if !defined(SMOKE_HAS_AVX2_BINARY)
    return false;
#elif defined(_MSC_VER)
    int cpu[4] = {0, 0, 0, 0};
    __cpuid(cpu, 0);
    if (cpu[0] < 7) return false;
    __cpuid(cpu, 1);
    if (!(cpu[2] & (1 << 27)) || !(cpu[2] & (1 << 28))) return false;  // OSXSAVE and AVX
    if ((_xgetbv(0) & 0x6) != 0x6) return false;                        // OS saves the YMM registers
    __cpuidex(cpu, 7, 0);
    return (cpu[1] & (1 << 5)) != 0;                                    // AVX2
#elif defined(__i386__) || defined(__x86_64__)
    __builtin_cpu_init();  // GCC/Clang: also checks that the OS saves the YMM registers
    return __builtin_cpu_supports("avx2") != 0;
#else
    return false;
#endif
}

} // namespace smoke
