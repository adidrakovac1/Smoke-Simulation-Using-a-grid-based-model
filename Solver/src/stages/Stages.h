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

/// 2. Buoyancy, optionally vorticity confinement (docs/plan/MATH.md, section 3). TODO(M2) / TODO(M3).
void applyForces(SmokeState& state, const SmokeParams& params, float dt, SlabRange range);

/// 3. Semi-Lagrangian advection of the velocity field (docs/plan/MATH.md, section 4). TODO(M2).
void advectVelocity(SmokeState& state, float dt, SlabRange range);

/// 4. Pressure projection: divergence, Jacobi iterations, velocity correction
///    (docs/plan/MATH.md, section 5). The most expensive phase. TODO(M2).
void project(SmokeState& state, const SmokeParams& params, SlabRange range);

/// 5. Semi-Lagrangian advection of density and temperature by the (corrected) velocity.
///    Swaps each field with its *0 partner, advects from *0 into the main field, then setBoundary.
void advectScalars(SmokeState& state, float dt, SlabRange range);

} // namespace smoke::serial
