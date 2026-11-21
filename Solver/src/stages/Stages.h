#pragma once

// The five phases of one simulation step plus boundary handling.
// Each phase is a free function in smoke::serial that gets everything it needs through its
// arguments: no globals, no hidden static buffers (docs/plan/ARCHITECTURE.md, section 5).
//
// This is the SERIAL reference implementation. Later variants (OpenMP, SIMD, GPU) are added in
// their own namespaces and this one stays as the correctness/speed reference.
//
// Invariant: every phase that writes a field calls setBoundary() for it at the end, so the ghost layer
// is always up to date for the next phase (docs/plan/MATH.md, section 6).

#include "SmokeState.h"
#include "smoke/Field3D.h"
#include "smoke/SmokeParams.h"

namespace smoke {
struct ObstacleLists;
}

namespace smoke::serial {

/// Range of interior z-planes [k0, k1) a call is responsible for. The serial code always passes the
/// whole interior {1, nz + 1}. A threaded version can later split this range between threads without
/// changing any algorithm.
struct SlabRange {
    int k0 = 1;
    int k1 = 1;
};

inline SlabRange wholeInterior(const GridSize& s) { return {1, s.nz + 1}; }

/// How the ghost layer of a field is filled (Stam 2003, set_bnd):
///   Scalar     ghost = nearest interior value (zero normal gradient): density, temperature, pressure;
///   VelocityX  like Scalar, but the sign flips on the two walls normal to x (no flow through them);
///   VelocityY / VelocityZ  the same for the walls normal to y / z.
enum class BoundaryKind { Scalar, VelocityX, VelocityY, VelocityZ };

/// Fills the whole ghost layer of `field`, including its edges and corners (docs/plan/MATH.md, section 6).
/// Always works on the whole field, not on a SlabRange.
void setBoundary(Field3D& field, BoundaryKind kind);

/// setBoundary for u (VelocityX), v (VelocityY) and w (VelocityZ).
void setVelocityBoundary(SmokeState& state);

/// D19: the part of setBoundary that belongs to the interior planes [range.k0, range.k1): their x and y ghost
/// cells, plus the ghost plane k = 0 (or nz + 1) when plane 1 (or nz) is in the range. Slabs that together cover
/// [1, nz] give exactly setBoundary, in any order or in parallel (Boundary.cpp explains why).
void setBoundarySlab(Field3D& field, BoundaryKind kind, SlabRange range);
void setVelocityBoundarySlab(SmokeState& state, SlabRange range);

/// A team of threads that runs one phase together (D19). tid in [0, size); sync() is a barrier for all of
/// them (does nothing for a team of one). SmokeSolver::step builds it; the serial backend is a team of one.
struct Team {
    int   tid  = 0;
    int   size = 1;
    void (*barrierFn)(void*) = nullptr;
    void* barrierCtx         = nullptr;
    bool  blockingWait       = false;  // D20: a barrier costs microseconds (threads sleep), so use fewer of them
    void  sync() const {
        if (size > 1) barrierFn(barrierCtx);
    }
};

/// Which build of a kernel to run (M4-009, M4-010, M4-014). Auto uses the AVX2 build when the CPU supports
/// it, otherwise the default build; Scalar forces the default build; Avx2 forces the AVX2 build and the
/// function returns false (doing nothing) when it is not available; Avx2Intrinsics forces a hand-written
/// AVX2 version (Jacobi, and advection since D19; same rule). All builds give bit-identical results. Tests
/// and smoke_lab use the explicit values to compare them.
enum class KernelIsa { Auto, Scalar, Avx2, Avx2Intrinsics };

/// Semi-Lagrangian advection of one field (docs/plan/MATH.md, section 4; Stam 1999 and 2003, advect):
/// for each interior cell, trace back x - dt*u, clamp to [0.5, n + 0.5] and interpolate `src` trilinearly.
/// `dst` and `src` must be different fields. Does NOT call setBoundary (the caller knows the field kind).
/// `isa`: see KernelIsa (AVX2 build since M4-014).
bool advectScalar(Field3D& dst, const Field3D& src, const Field3D& u, const Field3D& v, const Field3D& w,
                  float dt, SlabRange range, KernelIsa isa = KernelIsa::Auto);

/// advectScalar for `count` (1, 2 or 3) fields src[n] -> dst[n] carried by the same velocity, in one pass
/// (kernel fusion, M4-015): the back-trace, clamp and interpolation weights are computed once per cell.
/// Bit-identical to `count` separate advectScalar calls. Every dst[n] must differ from all src fields;
/// a src may be one of u, v, w.
bool advectFields(Field3D* const dst[], const Field3D* const src[], int count, const Field3D& u, const Field3D& v,
                  const Field3D& w, float dt, SlabRange range, KernelIsa isa = KernelIsa::Auto);

/// 1. Inject smoke (and heat) inside the source sphere: += dt * rate (docs/plan/MATH.md, section 3).
void addSources(SmokeState& state, const SmokeParams& params, float dt, SlabRange range);
/// addSources without the ghost layer (D19); returns the planes it may have changed (Sources.cpp).
SlabRange addSourcesSlab(SmokeState& state, const SmokeParams& params, float dt, SlabRange range);

/// v += dt * ( -alpha * rho + beta * (T - T_amb) )  (Fedkiw et al. 2001, eq. 8; +y is up), then setBoundary(v).
void applyBuoyancy(SmokeState& state, const SmokeParams& params, float dt, SlabRange range);
void applyBuoyancySlab(SmokeState& state, const SmokeParams& params, float dt, SlabRange range);

/// 2. Forces: buoyancy and optional vorticity confinement (docs/plan/MATH.md, section 3).
void applyForces(SmokeState& state, const SmokeParams& params, float dt, SlabRange range);
/// Add Fedkiw et al. vorticity confinement to the velocity field. A non-positive epsilon disables it.
/// `isa`: see KernelIsa (AVX2 build since M4-020); returns false only when the requested build is not available.
bool applyVorticityConfinement(SmokeState& state, float epsilon, float dt, SlabRange range,
                               KernelIsa isa = KernelIsa::Auto);
/// The two passes of applyVorticityConfinement for one slab, without the ghost layers (D19, Forces.cpp):
/// pass 1 writes |w| (vorticityMagnitude); pass 2 (after ALL of pass 1 and setBoundary of |w|) writes
/// u + eps*dt*(N x w) into u0 (and v0, w0), leaving u, v, w unchanged. epsilonDt = epsilon * dt.
void vorticityPassSlab(SmokeState& state, SlabRange range);
void confinementPassSlab(SmokeState& state, float epsilonDt, SlabRange range);

/// 3. Semi-Lagrangian advection of u, v, w by the old velocity (docs/plan/MATH.md, section 4).
void advectVelocity(SmokeState& state, float dt, SlabRange range);

/// div = ( u(i+1) - u(i-1) + v(j+1) - v(j-1) + w(k+1) - w(k-1) ) / 2   (h = 1; docs/plan/MATH.md, section 5).
/// Writes interior cells of `div` only.
void computeDivergence(const Field3D& u, const Field3D& v, const Field3D& w, Field3D& div, SlabRange range);

/// One Jacobi iteration for  laplacian(p) = div:  pNew = ( sum of the 6 neighbours of pOld - div ) / 6.
/// Reads only pOld, writes only pNew (interior), so the cells are independent of each other.
/// `isa`: see KernelIsa (M4-009, M4-010).
bool jacobiIteration(Field3D& pNew, const Field3D& pOld, const Field3D& div, SlabRange range,
                     KernelIsa isa = KernelIsa::Auto);

/// Rows [j0, j1) of plane k of one Jacobi iteration (interior cells); fromZero = first iteration of a step,
/// when the old iterate is all +0.0 and is not read. jacobiPlaneRowsKernel() returns the AVX2 build when the
/// CPU supports it. Same arithmetic as jacobiIteration: bit-identical.
using JacobiPlaneRowsFn = void (*)(float* pNew, const float* pOld, const float* div, int nx, int j0, int j1, int k,
                                   std::ptrdiff_t sJ, std::ptrdiff_t sK, bool fromZero);
JacobiPlaneRowsFn jacobiPlaneRowsKernel();

/// D19: `iterations` Jacobi iterations with a TEMPORAL BLOCKING (wavefront) schedule, run by every thread of
/// `team` (JacobiWavefront.cpp explains the schedule). Same result, bit for bit, as the loop in project():
///     pressure.fill(0); repeat: swap(pressure, pressure0); jacobiIteration; extendIntoSolids; setBoundary,
/// including pressure0 (the iterate before the last one) and every ghost cell. `waveDepth` is how many
/// iterations travel through the grid together, `planesPerTick` how many planes each of them advances between
/// two barriers (0 = automatic choice for both). All threads of the team must call it.
/// warmStart (D20): start from the values already in `pressure` (the previous step) instead of p = 0.
void jacobiWavefront(Field3D& pressure, Field3D& pressure0, const Field3D& div, int iterations, int waveDepth,
                     const ObstacleLists* obstacles, const Team& team, int planesPerTick = 0, bool warmStart = false);

/// D20: one colour sweep of red-black Gauss-Seidel / SOR over the planes [k0, k1), in place (RedBlackRows.inc).
/// colour 0 = cells with (i + j + k) even, 1 = odd. omega = 1: Gauss-Seidel; otherwise over-relaxation.
using RedBlackSweepFn = void (*)(float* p, const float* div, int nx, int ny, int k0, int k1, int colour, float omega,
                                 std::ptrdiff_t sJ, std::ptrdiff_t sK);
RedBlackSweepFn redBlackSweepKernel(KernelIsa isa = KernelIsa::Auto);

/// D20: `iterations` red-black iterations (red sweep, then black sweep) for laplacian(p) = div, in place in
/// `pressure`, run by every thread of `team` (RedBlack.cpp). warmStart = false starts from p = 0 like project();
/// true starts from the values already in `pressure` (the previous step). After every iteration the obstacle
/// cells (extendIntoSolids) and the ghost layer are refreshed, as in the Jacobi loop. The result does not
/// depend on the number of threads (the cells of one colour are independent).
void redBlackSolve(Field3D& pressure, const Field3D& div, int iterations, float omega, bool warmStart,
                   const ObstacleLists* obstacles, const Team& team);

/// u -= ( p(i+1) - p(i-1) ) / 2, and the same for v (in j) and w (in k). Interior cells only.
void subtractPressureGradient(Field3D& u, Field3D& v, Field3D& w, const Field3D& p, SlabRange range);

/// 4. Pressure projection (docs/plan/MATH.md, section 5; Stam 2003, project): divergence, p = 0,
///    params.jacobiIterations Jacobi iterations, subtract the gradient. The most expensive phase.
///    Afterwards state.divergence still holds the divergence BEFORE the correction (used by Diagnostics).
///    When provided, jacobiIterationMs receives the mean time per Jacobi iteration.
///    With obstacles (not null): after every Jacobi iteration the pressure is copied into the solid cells
///    next to the fluid (extendIntoSolids), and after the correction the velocity in solid cells is set again
///    (reflectVelocityIntoSolids).
void project(SmokeState& state, const SmokeParams& params, SlabRange range,
             double* jacobiIterationMs = nullptr, const ObstacleLists* obstacles = nullptr);

/// D19 (kernel fusion, Advection.cpp): for the cells of `range`, subtractPressureGradient followed by the
/// advection of density0 -> density and temperature0 -> temperature with the corrected velocity, in one pass.
/// The caller has swapped density/density0 and temperature/temperature0 before (as advectScalars does) and
/// fills the ghost layers after. Bit-identical to the two passes.
void correctVelocityAndAdvectScalars(SmokeState& state, float dt, SlabRange range);

/// 5. Semi-Lagrangian advection of density and temperature by the (corrected) velocity.
///    Swaps each field with its *0 partner, advects from *0 into the main field, then setBoundary.
void advectScalars(SmokeState& state, float dt, SlabRange range);

// -- Obstacles (docs/plan/OBSTACLES.md, decision D16): short loops over the sparse lists in ObstacleLists.h.
//    Called by SmokeSolver::step() between the phases, only when obstacles are set. --

/// Every solid cell next to the fluid gets the mean of its non-solid neighbours (zero difference across the
/// obstacle surface, like the Scalar rule of setBoundary at the box walls).
void extendIntoSolids(Field3D& field, const ObstacleLists& obstacles);
/// field = value in every solid cell.
void setInSolids(Field3D& field, float value, const ObstacleLists& obstacles);
/// Velocity in solid cells: 0 inside the obstacle; in the solid cells next to the fluid the normal component is
/// the negative of the fluid neighbour's and the others are copied, so the normal velocity ON the surface is 0
/// (the VelocityX/Y/Z rule of setBoundary, applied to the obstacle surface).
void reflectVelocityIntoSolids(Field3D& u, Field3D& v, Field3D& w, const ObstacleLists& obstacles);
/// reflectVelocityIntoSolids for the state, plus the ghost layer when an obstacle touches a wall.
void enforceSolidVelocity(SmokeState& state, const ObstacleLists& obstacles);
/// density = 0 and temperature = ambient in every solid cell.
void clearSolidScalars(SmokeState& state, float ambientTemperature, const ObstacleLists& obstacles);
/// Velocity in porous cells *= 1 / (1 + dt * sigma).
void applyPorousDrag(SmokeState& state, float dt, const ObstacleLists& obstacles);
/// Density in porous cells *= 1 / (1 + dt * phi).
void applyPorousFilter(SmokeState& state, float dt, const ObstacleLists& obstacles);

} // namespace smoke::serial
