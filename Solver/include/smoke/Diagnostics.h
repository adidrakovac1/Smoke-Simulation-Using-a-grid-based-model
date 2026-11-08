#pragma once

namespace smoke {

/// Numbers that tell us whether the simulation is healthy (docs/plan/VERIFICATION.md, section 2).
struct Diagnostics {
    bool      computed        = false;  // false until the stages that fill these values exist
    double    maxDivBefore    = 0.0;    // max |div u| before the pressure projection
    double    maxDivAfter     = 0.0;    // max |div u| after the pressure projection
    double    totalMass       = 0.0;    // sum of density over interior cells
    double    maxSpeed        = 0.0;    // max |u|
    double    cfl             = 0.0;    // max|u| * dt / h
    long long nonFiniteCells  = 0;      // number of NaN / Inf values (must be 0)
};

} // namespace smoke
