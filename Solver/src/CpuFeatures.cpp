#include "CpuFeatures.h"

#if defined(SMOKE_HAS_AVX2_BINARY) && defined(_MSC_VER)
#include <immintrin.h>
#include <intrin.h>
#endif

namespace smoke {

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
