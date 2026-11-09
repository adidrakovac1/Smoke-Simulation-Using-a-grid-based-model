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
