#pragma once

#include "SmokeState.h"
#include "smoke/Diagnostics.h"

namespace smoke {
struct ObstacleLists;
}

namespace smoke {

/// Health numbers of the current state (docs/plan/VERIFICATION.md, section 2). Several full passes over the
/// grid, so it is called on demand (SmokeSolver::diagnostics()) and never inside the timed stages.
///   maxDivBefore  max |div| stored by the last project() (the divergence before the correction);
///   maxDivAfter   max |div| of the current velocity, computed here with the same formula;
///   totalMass     sum of density over interior cells;
///   maxSpeed      max |u| over interior cells;  cfl = maxSpeed * dt / h with h = 1 (decision D4);
///   nonFiniteCells  NaN / Inf count over density, temperature, u, v, w and pressure (ghost layer included).
/// With obstacles (not null), solid cells are left out of the divergence and speed numbers: the central
/// difference across an obstacle surface is not a property of the flow.
Diagnostics computeDiagnostics(const SmokeState& state, float dt, const ObstacleLists* obstacles = nullptr);

} // namespace smoke
