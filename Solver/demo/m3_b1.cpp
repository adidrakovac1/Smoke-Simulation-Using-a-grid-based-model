// M3 solver step with the default Release flags (the compiler may auto-vectorize; on x86-64 without -mavx2
// that means SSE), like the B1 build. See M3Solver.inc.
#include "DemoM3.h"
#include "Profiler.h"

#include <algorithm>
#include <cmath>

#define SMOKE_M3_NS m3_b1
#include "M3Solver.inc"
