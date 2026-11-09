#pragma once

namespace smoke {

/// Where smoke (and heat) is injected: a sphere inside the box. Position and radius are relative, so they
/// do not change when the grid size changes: x, y, z in [0,1] along each axis, radius as a fraction of nx.
/// Every step each cell inside the sphere gets  density += dt * densityRate  and
/// temperature += dt * temperatureRate  (like add_source in Stam 2003; docs/plan/MATH.md, section 3).
/// Default values: chosen experimentally on 64^3 by looking at the slices (2026-11-09, M2), recorded in
/// docs/plan/MATH.md, section 3. They are not from a paper and may be re-tuned in M3.
struct SmokeSource {
    float x = 0.5f, y = 0.1f, z = 0.5f;  // bottom centre of the box
    float radius          = 0.08f;       // about 5 cells on 64^3
    float densityRate     = 10.0f;
    float temperatureRate = 10.0f;
};

/// All values that can be changed while the program runs (e.g. from the ImGui menu later).
/// Units: the solver works in grid-cell units (h = 1), see docs/plan/PLAN.md, decision D4.
struct SmokeParams {
    float dt               = 1.0f / 60.0f;  // from the project plan: one step = 1/60 s
    int   jacobiIterations = 40;            // project plan says 20 to 80 per step
    // Buoyancy, Fedkiw et al. 2001, eq. 8. The paper gives no numbers ("two positive constants"); these were
    // chosen experimentally on 64^3 (2026-11-09, M2), see docs/plan/MATH.md, section 3.
    float buoyancyAlpha    = 0.05f;
    float buoyancyBeta     = 2.0f;
    float ambientTemp      = 0.0f;
    float vorticityEps     = 0.0f;          // 0 = vorticity confinement off
    float viscosity        = 0.0f;          // 0 = skip velocity diffusion (small for smoke)
    float diffusion        = 0.0f;          // 0 = skip density diffusion
    SmokeSource source;
};

} // namespace smoke
