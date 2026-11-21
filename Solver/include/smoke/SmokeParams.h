#pragma once

#include <array>

namespace smoke {

/// D22: how many smoke sources and fans one box can have (fixed arrays, no allocation in SmokeParams).
inline constexpr int kMaxSources = 8;
inline constexpr int kMaxFans    = 8;

/// Where smoke (and heat) is injected: a sphere inside the box. Position and radius are relative, so they
/// do not change when the grid size changes: x, y, z in [0,1] along each axis, radius as a fraction of nx.
/// Every step each cell inside the sphere gets  density += dt * densityRate  and
/// temperature += dt * temperatureRate  (like add_source in Stam 2003; docs/plan/MATH.md, section 3).
/// Source/buoyancy defaults were chosen experimentally on 64^3 in M2; the vorticity epsilon was
/// tuned visually in M3. Values and observations are recorded in docs/plan/MATH.md, section 3.
struct SmokeSource {
    float x = 0.5f, y = 0.1f, z = 0.5f;  // bottom centre of the box
    float radius          = 0.08f;       // about 5 cells on 64^3
    float densityRate     = 10.0f;
    float temperatureRate = 10.0f;
};

/// D22: a fan, an external force that pushes the air inside a cylinder in front of it along its axis
/// (docs/plan/MATH.md, section 3, "Ventilator"). Position like SmokeSource (relative, [0,1] along each axis);
/// radius and reach (the cylinder's length) as a fraction of nx, like SmokeSource::radius.
/// For a cell centre p:  a = (p - c) . n  (distance along the axis),  r^2 = |p - c|^2 - a^2;
/// if 0 <= a <= reach and r <= radius:  u += dt * strength * (1 - a / reach) * n.
/// The form u += dt * f is the external force term of Stam 2003 / Fedkiw et al. 2001; the cylinder and the
/// linear fall-off are our model choice.
struct SmokeFan {
    float x = 0.5f, y = 0.5f, z = 0.5f;          // centre of the fan's face
    float dirX = 1.0f, dirY = 0.0f, dirZ = 0.0f; // blowing direction; normalized by the solver (zero = off)
    float radius   = 0.06f;
    float reach    = 0.40f;
    float strength = 0.0f;  // cells/s^2; 0 = off. TODO(measure): chosen visually in the house scene (D22)
};

/// How the pressure equation  laplacian(p) = div  is solved (decision D20, docs/plan/MATH.md, section 5).
///   Jacobi                every cell from the OLD iterate of its neighbours (two buffers); the reference.
///   RedBlackGaussSeidel   the cells are coloured like a 3D chess board, (i + j + k) even = red, odd = black;
///                         first all red cells, then all black cells are updated IN PLACE, each from the newest
///                         values of its neighbours (which all have the other colour). With sorOmega > 1 the
///                         change of every cell is over-relaxed (SOR). Converges faster per iteration than
///                         Jacobi, but gives a DIFFERENT result (not bit-identical to the reference).
enum class PressureSolver { Jacobi, RedBlackGaussSeidel };

/// All values that can be changed while the program runs (e.g. from the ImGui menu later).
/// Units: the solver works in grid-cell units (h = 1), see docs/plan/PLAN.md, decision D4.
struct SmokeParams {
    float dt               = 1.0f / 60.0f;  // from the project plan: one step = 1/60 s
    int   jacobiIterations = 40;            // project plan says 20 to 80 per step; also the red-black iterations
    // D20. The defaults are the reference solver (Jacobi from p = 0), so --verify stays bit-identical.
    PressureSolver pressureSolver    = PressureSolver::Jacobi;
    float          sorOmega          = 1.0f;   // red-black only; 1 = plain Gauss-Seidel. TODO(measure): best value
    bool           warmStartPressure = false;  // true: start from the pressure of the previous step instead of 0
    // Buoyancy, Fedkiw et al. 2001, eq. 8. The paper gives no numbers ("two positive constants"); these were
    // chosen experimentally on 64^3 (2026-11-09, M2), see docs/plan/MATH.md, section 3.
    float buoyancyAlpha    = 0.05f;
    float buoyancyBeta     = 2.0f;
    float ambientTemp      = 0.0f;
    float vorticityEps     = 5.0f;          // M3 visual tuning for 64^3; 0 = off
    float viscosity        = 0.0f;          // 0 = skip velocity diffusion (small for smoke)
    float diffusion        = 0.0f;          // 0 = skip density diffusion
    SmokeSource source;
    // D22: more sources and fans. The defaults (none) keep the reference result bit for bit.
    std::array<SmokeSource, kMaxSources - 1> extraSources{};  // used: the first extraSourceCount
    int                                      extraSourceCount = 0;
    std::array<SmokeFan, kMaxFans>           fans{};          // used: the first fanCount
    int                                      fanCount = 0;
};

} // namespace smoke
