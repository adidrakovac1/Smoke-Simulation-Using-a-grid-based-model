#pragma once

#include "SmokeState.h"
#include "smoke/Diagnostics.h"

namespace smoke {

/// Health numbers of the current state (docs/plan/VERIFICATION.md, section 2). Several full passes over the
/// grid, so it is called on demand (SmokeSolver::diagnostics()) and never inside the timed stages.
///   maxDivBefore  max |div| stored by the last project() (the divergence before the correction);
///   maxDivAfter   max |div| of the current velocity, computed here with the same formula;
///   totalMass     sum of density over interior cells;
///   maxSpeed      max |u| over interior cells;  cfl = maxSpeed * dt / h with h = 1 (decision D4);
///   nonFiniteCells  NaN / Inf count over density, temperature, u, v, w and pressure (ghost layer included).
Diagnostics computeDiagnostics(const SmokeState& state, float dt);

} // namespace smoke
