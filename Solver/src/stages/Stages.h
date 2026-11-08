#pragma once

// The five phases of one simulation step plus boundary handling.
// Each phase is a free function in smoke::serial that gets everything it needs through its
// arguments: no globals, no hidden static buffers (docs/plan/ARCHITECTURE.md, section 5).
//
// This is the SERIAL reference implementation. Later variants (OpenMP, SIMD, GPU) are added in
// their own namespaces and this one stays as the correctness/speed reference.

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

enum class BoundaryKind { Scalar, VelocityX, VelocityY, VelocityZ };

/// Boundary conditions on the walls of the box (docs/plan/MATH.md, section 6). TODO(M1).
void setBoundary(Field3D& field, BoundaryKind kind);

/// 1. Inject smoke (and heat) into a few cells near the bottom. TODO(M1).
void addSources(SmokeState& state, const SmokeParams& params, SlabRange range);

/// 2. Buoyancy, optionally vorticity confinement (docs/plan/MATH.md, section 3). TODO(M2) / TODO(M3).
void applyForces(SmokeState& state, const SmokeParams& params, float dt, SlabRange range);

/// 3. Semi-Lagrangian advection of the velocity field (docs/plan/MATH.md, section 4). TODO(M2).
void advectVelocity(SmokeState& state, float dt, SlabRange range);

/// 4. Pressure projection: divergence, Jacobi iterations, velocity correction
///    (docs/plan/MATH.md, section 5). The most expensive phase. TODO(M2).
void project(SmokeState& state, const SmokeParams& params, SlabRange range);

/// 5. Semi-Lagrangian advection of density and temperature by the corrected velocity. TODO(M1).
void advectScalars(SmokeState& state, float dt, SlabRange range);

} // namespace smoke::serial
