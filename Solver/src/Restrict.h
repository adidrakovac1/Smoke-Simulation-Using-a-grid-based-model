#pragma once

// SMOKE_RESTRICT marks a pointer as the only way to reach its array inside a kernel (lab technique T12,
// docs/plan/OPTIMIZATION.md, section 2.1). With it the compiler may keep loads in registers and vectorise
// without runtime overlap checks. Use it only where the arrays really never overlap: breaking that promise
// is undefined behaviour. GCC, Clang and MSVC all spell it __restrict.
#if defined(__GNUC__) || defined(__clang__) || defined(_MSC_VER)
#define SMOKE_RESTRICT __restrict
#else
#define SMOKE_RESTRICT
#endif

// SMOKE_NOINLINE keeps a function out of line (M4-013). g++ vectorizes the advection row loop only when the row
// function stays a separate function: once it is inlined into the slab loop, the restrict promises on its
// pointer parameters no longer cover the velocity-dependent reads, and the loop is reported as
// "data ref analysis failed". One call per row costs nanoseconds; a row costs microseconds.
#if defined(__GNUC__) || defined(__clang__)
#define SMOKE_NOINLINE __attribute__((noinline))
#elif defined(_MSC_VER)
#define SMOKE_NOINLINE __declspec(noinline)
#else
#define SMOKE_NOINLINE
#endif
