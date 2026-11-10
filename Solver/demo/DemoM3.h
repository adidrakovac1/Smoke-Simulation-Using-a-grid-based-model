#pragma once
// The two compiled copies of the M3 solver step (M3Solver.inc): m3_b0 without vectorization, m3_b1 with the
// default Release flags. Same arithmetic as the optimized solver, so all three give bit-identical fields.
#include "SmokeState.h"
#include "smoke/SmokeParams.h"
#include "smoke/StepTimings.h"

namespace smoke_demo {
namespace m3_b0 {
void m3Step(smoke::SmokeState& s, const smoke::SmokeParams& p, smoke::StepTimings& t);
}
namespace m3_b1 {
void m3Step(smoke::SmokeState& s, const smoke::SmokeParams& p, smoke::StepTimings& t);
}
} // namespace smoke_demo
