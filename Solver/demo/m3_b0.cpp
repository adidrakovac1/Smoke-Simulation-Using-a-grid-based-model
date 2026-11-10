// "Neoptimizirana verzija": the M3 solver step compiled WITHOUT vectorization (CMake adds -fno-tree-vectorize
// to this file only), like the B0 build. See M3Solver.inc.
#include "DemoM3.h"
#include "Profiler.h"

#include <algorithm>
#include <cmath>

#define SMOKE_M3_NS m3_b0
#include "M3Solver.inc"
