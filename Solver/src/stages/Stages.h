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

/// Semi-Lagrangian advection of one field (docs/plan/MATH.md, section 4; Stam 1999 and 2003, advect):
/// for each interior cell, trace back x - dt*u, clamp to [0.5, n + 0.5] and interpolate `src` trilinearly.
/// `dst` and `src` must be different fields. Does NOT call setBoundary (the caller knows the field kind).
void advectScalar(Field3D& dst, const Field3D& src, const Field3D& u, const Field3D& v, const Field3D& w,
                  float dt, SlabRange range);

/// 1. Inject smoke (and heat) inside the source sphere: += dt * rate (docs/plan/MATH.md, section 3).
void addSources(SmokeState& state, const SmokeParams& params, float dt, SlabRange range);

/// v += dt * ( -alpha * rho + beta * (T - T_amb) )  (Fedkiw et al. 2001, eq. 8; +y is up), then setBoundary(v).
void applyBuoyancy(SmokeState& state, const SmokeParams& params, float dt, SlabRange range);

/// 2. Forces: buoyancy and optional vorticity confinement (docs/plan/MATH.md, section 3).
void applyForces(SmokeState& state, const SmokeParams& params, float dt, SlabRange range);
/// Add Fedkiw et al. vorticity confinement to the velocity field. A non-positive epsilon disables it.
void applyVorticityConfinement(SmokeState& state, float epsilon, float dt, SlabRange range);

/// 3. Semi-Lagrangian advection of u, v, w by the old velocity (docs/plan/MATH.md, section 4).
void advectVelocity(SmokeState& state, float dt, SlabRange range);

/// div = ( u(i+1) - u(i-1) + v(j+1) - v(j-1) + w(k+1) - w(k-1) ) / 2   (h = 1; docs/plan/MATH.md, section 5).
/// Writes interior cells of `div` only.
void computeDivergence(const Field3D& u, const Field3D& v, const Field3D& w, Field3D& div, SlabRange range);

/// One Jacobi iteration for  laplacian(p) = div:  pNew = ( sum of the 6 neighbours of pOld - div ) / 6.
/// Reads only pOld, writes only pNew (interior), so the cells are independent of each other.
void jacobiIteration(Field3D& pNew, const Field3D& pOld, const Field3D& div, SlabRange range);

/// u -= ( p(i+1) - p(i-1) ) / 2, and the same for v (in j) and w (in k). Interior cells only.
void subtractPressureGradient(Field3D& u, Field3D& v, Field3D& w, const Field3D& p, SlabRange range);

/// 4. Pressure projection (docs/plan/MATH.md, section 5; Stam 2003, project): divergence, p = 0,
///    params.jacobiIterations Jacobi iterations, subtract the gradient. The most expensive phase.
///    Afterwards state.divergence still holds the divergence BEFORE the correction (used by Diagnostics).
///    When provided, jacobiIterationMs receives the mean time per Jacobi iteration.
void project(SmokeState& state, const SmokeParams& params, SlabRange range,
             double* jacobiIterationMs = nullptr);

/// 5. Semi-Lagrangian advection of density and temperature by the (corrected) velocity.
///    Swaps each field with its *0 partner, advects from *0 into the main field, then setBoundary.
void advectScalars(SmokeState& state, float dt, SlabRange range);

} // namespace smoke::serial
