#pragma once

namespace smoke {

/// Where smoke (and heat) is injected. Position is relative to the simulation box, each
/// component in [0,1] (so it does not change when the grid size changes).
struct SmokeSource {
    float x = 0.5f, y = 0.1f, z = 0.5f;  // placeholder position near the bottom centre of the box
    float radiusCells = 0.0f;            // TODO(measure): choose together with the visual result (M1/M3)
    float densityRate = 0.0f;            // TODO(measure)
    float temperature = 0.0f;            // TODO(measure)
};

/// All values that can be changed while the program runs (e.g. from the ImGui menu later).
/// Units: the solver works in grid-cell units (h = 1), see docs/plan/PLAN.md, decision D4.
struct SmokeParams {
    float dt               = 1.0f / 60.0f;  // from the project plan: one step = 1/60 s
    int   jacobiIterations = 40;            // project plan says 20 to 80 per step
    float buoyancyAlpha    = 0.0f;          // TODO(paper: Fedkiw et al. 2001, eq. 8): tune visually
    float buoyancyBeta     = 0.0f;          // TODO(paper: Fedkiw et al. 2001, eq. 8): tune visually
    float ambientTemp      = 0.0f;
    float vorticityEps     = 0.0f;          // 0 = vorticity confinement off
    float viscosity        = 0.0f;          // 0 = skip velocity diffusion (small for smoke)
    float diffusion        = 0.0f;          // 0 = skip density diffusion
    SmokeSource source;
};

} // namespace smoke
