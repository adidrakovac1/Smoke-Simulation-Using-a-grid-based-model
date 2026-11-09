#pragma once

namespace smoke {

/// Where smoke (and heat) is injected: a sphere inside the box. Position and radius are relative, so they
/// do not change when the grid size changes: x, y, z in [0,1] along each axis, radius as a fraction of nx.
/// Every step each cell inside the sphere gets  density += dt * densityRate  and
/// temperature += dt * temperatureRate  (like add_source in Stam 2003; docs/plan/MATH.md, section 3).
struct SmokeSource {
    float x = 0.5f, y = 0.1f, z = 0.5f;  // placeholder position near the bottom centre of the box
    float radius          = 0.0f;        // TODO(measure): choose together with the visual result (M2)
    float densityRate     = 0.0f;        // TODO(measure)
    float temperatureRate = 0.0f;        // TODO(measure)
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
