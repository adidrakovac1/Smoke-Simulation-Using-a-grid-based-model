// The WHOLE M3 solver step (commit 73ceb9e, Solver/src/stages/*.cpp and Interpolation.h), copied here
// unchanged as history, so smoke_demo can run the unoptimized simulation next to the optimized one in the
// same program. It is compiled twice (like the lab example's *_scalar.cpp files):
//   m3_b0.cpp  with vectorization switched off  -> "Neoptimizirana verzija" (like the B0 build)
//   m3_b1.cpp  with the default Release flags   -> M3 + automatic vectorization of the compiler (like B1)
// Include it only from those two files, after defining SMOKE_M3_NS (a different namespace for each), so the
// two copies never clash. Only the bodies below come from 73ceb9e; the `using` lines, the declarations and
// m3Step() (the order of SmokeSolver::step in 73ceb9e) were added for the demo.

namespace smoke_demo::SMOKE_M3_NS {

using smoke::Field3D;
using smoke::GridSize;
using smoke::ScopedTimer;
using smoke::SmokeParams;
using smoke::SmokeSource;
using smoke::SmokeState;
// Own copies of the small types of Stages.h (73ceb9e). Stages.h itself is NOT included: with its
// smoke::serial types, argument-dependent lookup would also find the optimized functions of the same name.
struct SlabRange {
    int k0 = 1;
    int k1 = 1;
};
inline SlabRange wholeInterior(const GridSize& s) { return {1, s.nz + 1}; }
enum class BoundaryKind { Scalar, VelocityX, VelocityY, VelocityZ };

void setBoundary(Field3D& field, BoundaryKind kind);
void setVelocityBoundary(SmokeState& state);
void applyVorticityConfinement(SmokeState& state, float epsilon, float dt, SlabRange range);

// ----- Interpolation.h (73ceb9e) -----
/// Clamp a back-traced coordinate to the box. Interior cell i covers [i - 0.5, i + 0.5], so the box
/// spans [0.5, n + 0.5] along an axis with n interior cells. This is the clamp used by advect() in
/// Stam 2003, and it matches clipping the path at the wall in Fedkiw et al. 2001, fig. 2
/// (docs/plan/MATH.md, section 4).
inline float clampToBox(float x, int n) {
    const float lo = 0.5f;
    const float hi = static_cast<float>(n) + 0.5f;
    return x < lo ? lo : (x > hi ? hi : x);
}

/// Trilinear interpolation of `f` at the point (x, y, z), given in cell-index coordinates
/// (cell (i,j,k) has its centre at (i,j,k)). The point must already be clamped with clampToBox, so all
/// eight neighbours exist (they may be ghost cells, which setBoundary keeps up to date).
///
/// The result is a weighted average of the 8 surrounding values with weights >= 0 that sum to 1, so it
/// always lies between their minimum and maximum (test T4, docs/plan/VERIFICATION.md).
inline float sampleTrilinear(const Field3D& f, float x, float y, float z) {
    // x >= 0.5 after clamping, so the int conversion (which truncates) is the same as floor().
    const int i0 = static_cast<int>(x);
    const int j0 = static_cast<int>(y);
    const int k0 = static_cast<int>(z);
    const int i1 = i0 + 1;
    const int j1 = j0 + 1;
    const int k1 = k0 + 1;

    const float sx1 = x - static_cast<float>(i0), sx0 = 1.0f - sx1;
    const float sy1 = y - static_cast<float>(j0), sy0 = 1.0f - sy1;
    const float sz1 = z - static_cast<float>(k0), sz0 = 1.0f - sz1;

    // Same nesting as Stam 2003 advect(), extended by one level for z.
    return sz0 * (sy0 * (sx0 * f.at(i0, j0, k0) + sx1 * f.at(i1, j0, k0)) +
                  sy1 * (sx0 * f.at(i0, j1, k0) + sx1 * f.at(i1, j1, k0))) +
           sz1 * (sy0 * (sx0 * f.at(i0, j0, k1) + sx1 * f.at(i1, j0, k1)) +
                  sy1 * (sx0 * f.at(i0, j1, k1) + sx1 * f.at(i1, j1, k1)));
}

// ----- Sources.cpp (73ceb9e) -----



// Like add_source in Stam 2003 (x += dt * s), but the source "s" is a sphere given by params.source
// instead of a user-filled array (docs/plan/MATH.md, section 3).
void addSources(SmokeState& state, const SmokeParams& params, float dt, SlabRange range) {
    const SmokeSource& src = params.source;
    const GridSize     g   = state.density.size();

    // Relative position -> cell-index coordinates. The box spans [0.5, n + 0.5], so x = 0.5 maps to the
    // exact middle (n + 1) / 2, which keeps a centred source mirror-symmetric (test T6).
    const float cx     = 0.5f + src.x * static_cast<float>(g.nx);
    const float cy     = 0.5f + src.y * static_cast<float>(g.ny);
    const float cz     = 0.5f + src.z * static_cast<float>(g.nz);
    const float radius = src.radius * static_cast<float>(g.nx);
    if (radius <= 0.0f) return;
    const float r2 = radius * radius;

    // Only visit the bounding box of the sphere (clipped to the interior and to this call's slab).
    const int i0 = std::max(1, static_cast<int>(std::floor(cx - radius)));
    const int i1 = std::min(g.nx, static_cast<int>(std::ceil(cx + radius)));
    const int j0 = std::max(1, static_cast<int>(std::floor(cy - radius)));
    const int j1 = std::min(g.ny, static_cast<int>(std::ceil(cy + radius)));
    const int k0 = std::max(range.k0, static_cast<int>(std::floor(cz - radius)));
    const int k1 = std::min(range.k1 - 1, static_cast<int>(std::ceil(cz + radius)));

    const float addDensity     = dt * src.densityRate;
    const float addTemperature = dt * src.temperatureRate;

    for (int k = k0; k <= k1; ++k) {
        for (int j = j0; j <= j1; ++j) {
            for (int i = i0; i <= i1; ++i) {
                const float dx = static_cast<float>(i) - cx;
                const float dy = static_cast<float>(j) - cy;
                const float dz = static_cast<float>(k) - cz;
                if (dx * dx + dy * dy + dz * dz <= r2) {
                    state.density.at(i, j, k)     += addDensity;
                    state.temperature.at(i, j, k) += addTemperature;
                }
            }
        }
    }

    setBoundary(state.density, BoundaryKind::Scalar);
    setBoundary(state.temperature, BoundaryKind::Scalar);
}

// ----- Forces.cpp (73ceb9e) -----



// Buoyancy, Fedkiw et al. 2001, eq. (8):  f = ( -alpha * rho + beta * (T - T_amb) ) * up.
// Heavy smoke is pulled down (alpha), hot gas is pushed up (beta). In our box "up" is +y
// (docs/plan/ARCHITECTURE.md, section 8), so only v changes. The force is added as v += dt * f
// (Fedkiw et al. 2001, section 4; Stam 1999, addForce). Forces are cell-centred like everything else.
void applyBuoyancy(SmokeState& s, const SmokeParams& p, float dt, SlabRange range) {
    const GridSize g     = s.v.size();
    const float    alpha = p.buoyancyAlpha;
    const float    beta  = p.buoyancyBeta;
    const float    tAmb  = p.ambientTemp;
    for (int k = range.k0; k < range.k1; ++k) {
        for (int j = 1; j <= g.ny; ++j) {
            for (int i = 1; i <= g.nx; ++i) {
                const float force = -alpha * s.density.at(i, j, k) + beta * (s.temperature.at(i, j, k) - tAmb);
                s.v.at(i, j, k) += dt * force;
            }
        }
    }
    setBoundary(s.v, BoundaryKind::VelocityY);
}

void applyForces(SmokeState& state, const SmokeParams& params, float dt, SlabRange range) {
    applyBuoyancy(state, params, dt, range);
    applyVorticityConfinement(state, params.vorticityEps, dt, range);
}

void applyVorticityConfinement(SmokeState& s, float epsilon, float dt, SlabRange range) {
    if (!(epsilon > 0.0f)) return;

    const GridSize g = s.u.size();
    for (int k = range.k0; k < range.k1; ++k) {
        for (int j = 1; j <= g.ny; ++j) {
            for (int i = 1; i <= g.nx; ++i) {
                const float wx = 0.5f * ((s.w.at(i, j + 1, k) - s.w.at(i, j - 1, k)) -
                                         (s.v.at(i, j, k + 1) - s.v.at(i, j, k - 1)));
                const float wy = 0.5f * ((s.u.at(i, j, k + 1) - s.u.at(i, j, k - 1)) -
                                         (s.w.at(i + 1, j, k) - s.w.at(i - 1, j, k)));
                const float wz = 0.5f * ((s.v.at(i + 1, j, k) - s.v.at(i - 1, j, k)) -
                                         (s.u.at(i, j + 1, k) - s.u.at(i, j - 1, k)));
                s.vorticityX.at(i, j, k) = wx;
                s.vorticityY.at(i, j, k) = wy;
                s.vorticityZ.at(i, j, k) = wz;
                s.vorticityMagnitude.at(i, j, k) = std::sqrt(wx * wx + wy * wy + wz * wz);
            }
        }
    }
    setBoundary(s.vorticityMagnitude, BoundaryKind::Scalar);

    for (int k = range.k0; k < range.k1; ++k) {
        for (int j = 1; j <= g.ny; ++j) {
            for (int i = 1; i <= g.nx; ++i) {
                const float nx = 0.5f * (s.vorticityMagnitude.at(i + 1, j, k) -
                                         s.vorticityMagnitude.at(i - 1, j, k));
                const float ny = 0.5f * (s.vorticityMagnitude.at(i, j + 1, k) -
                                         s.vorticityMagnitude.at(i, j - 1, k));
                const float nz = 0.5f * (s.vorticityMagnitude.at(i, j, k + 1) -
                                         s.vorticityMagnitude.at(i, j, k - 1));
                const float gradientMagnitude = std::sqrt(nx * nx + ny * ny + nz * nz);
                if (gradientMagnitude > 0.0f) {
                    const float scale = epsilon * dt / gradientMagnitude;  // h = 1 cell
                    const float wx = s.vorticityX.at(i, j, k);
                    const float wy = s.vorticityY.at(i, j, k);
                    const float wz = s.vorticityZ.at(i, j, k);
                    s.u.at(i, j, k) += scale * (ny * wz - nz * wy);
                    s.v.at(i, j, k) += scale * (nz * wx - nx * wz);
                    s.w.at(i, j, k) += scale * (nx * wy - ny * wx);
                }
            }
        }
    }
    setVelocityBoundary(s);
}

// ----- Advection.cpp (73ceb9e) -----


// Semi-Lagrangian advection (Stam 1999, section 2.2 and appendix A; Stam 2003, advect()):
// "where did the fluid now at this cell centre come from dt seconds ago?" Trace back one Euler step,
// clamp to the box, and take the old value there by trilinear interpolation. With h = 1 (decision D4)
// the velocity is in cells per second, so the back-traced position is simply  i - dt * u.
void advectScalar(Field3D& dst, const Field3D& src, const Field3D& u, const Field3D& v, const Field3D& w,
                  float dt, SlabRange range) {
    const GridSize s = dst.size();
    for (int k = range.k0; k < range.k1; ++k) {
        for (int j = 1; j <= s.ny; ++j) {
            for (int i = 1; i <= s.nx; ++i) {
                const float x = clampToBox(static_cast<float>(i) - dt * u.at(i, j, k), s.nx);
                const float y = clampToBox(static_cast<float>(j) - dt * v.at(i, j, k), s.ny);
                const float z = clampToBox(static_cast<float>(k) - dt * w.at(i, j, k), s.nz);
                dst.at(i, j, k) = sampleTrilinear(src, x, y, z);
            }
        }
    }
}

// Self-advection of the velocity (Stam 1999, section 2.2): each component is a field like any other and is
// carried by the flow. All three components are traced back with the OLD velocity (u0, v0, w0), so the
// result does not depend on the order in which components or cells are processed.
void advectVelocity(SmokeState& s, float dt, SlabRange range) {
    s.u.swap(s.u0);
    s.v.swap(s.v0);
    s.w.swap(s.w0);
    advectScalar(s.u, s.u0, s.u0, s.v0, s.w0, dt, range);
    advectScalar(s.v, s.v0, s.u0, s.v0, s.w0, dt, range);
    advectScalar(s.w, s.w0, s.u0, s.v0, s.w0, dt, range);
    setVelocityBoundary(s);
}

void advectScalars(SmokeState& s, float dt, SlabRange range) {
    // Double buffering: after the swap, density0 holds the old values and density is overwritten.
    s.density.swap(s.density0);
    advectScalar(s.density, s.density0, s.u, s.v, s.w, dt, range);
    setBoundary(s.density, BoundaryKind::Scalar);

    s.temperature.swap(s.temperature0);
    advectScalar(s.temperature, s.temperature0, s.u, s.v, s.w, dt, range);
    setBoundary(s.temperature, BoundaryKind::Scalar);
}

// ----- Projection.cpp (73ceb9e) -----



// Pressure projection (Stam 1999, appendix B; Stam 2003, project(); docs/plan/MATH.md, section 5).
//
// Incompressible air must have zero divergence: as much flows into a cell as flows out. After forces and
// advection that is no longer true. We find a pressure p with  laplacian(p) = div(u)  and subtract its
// gradient, which removes (most of) the divergence. Sign check against Stam 2003: there
// div_S = -0.5*h*(...) and p = (div_S + sum)/4; our div has the opposite sign, so p = (sum - div)/6 is the
// same equation in 3D. Compared with Fedkiw et al. 2001, eq. (4)-(5), our p is dt * p (MATH.md, section 5).

void computeDivergence(const Field3D& u, const Field3D& v, const Field3D& w, Field3D& div, SlabRange range) {
    const GridSize g = div.size();
    for (int k = range.k0; k < range.k1; ++k) {
        for (int j = 1; j <= g.ny; ++j) {
            for (int i = 1; i <= g.nx; ++i) {
                div.at(i, j, k) = 0.5f * (u.at(i + 1, j, k) - u.at(i - 1, j, k) +
                                          v.at(i, j + 1, k) - v.at(i, j - 1, k) +
                                          w.at(i, j, k + 1) - w.at(i, j, k - 1));
            }
        }
    }
}

void jacobiIteration(Field3D& pNew, const Field3D& pOld, const Field3D& div, SlabRange range) {
    const GridSize g = pNew.size();
    for (int k = range.k0; k < range.k1; ++k) {
        for (int j = 1; j <= g.ny; ++j) {
            for (int i = 1; i <= g.nx; ++i) {
                const float neighbours = pOld.at(i - 1, j, k) + pOld.at(i + 1, j, k) +
                                         pOld.at(i, j - 1, k) + pOld.at(i, j + 1, k) +
                                         pOld.at(i, j, k - 1) + pOld.at(i, j, k + 1);
                pNew.at(i, j, k) = (neighbours - div.at(i, j, k)) / 6.0f;
            }
        }
    }
}

void subtractPressureGradient(Field3D& u, Field3D& v, Field3D& w, const Field3D& p, SlabRange range) {
    const GridSize g = p.size();
    for (int k = range.k0; k < range.k1; ++k) {
        for (int j = 1; j <= g.ny; ++j) {
            for (int i = 1; i <= g.nx; ++i) {
                u.at(i, j, k) -= 0.5f * (p.at(i + 1, j, k) - p.at(i - 1, j, k));
                v.at(i, j, k) -= 0.5f * (p.at(i, j + 1, k) - p.at(i, j - 1, k));
                w.at(i, j, k) -= 0.5f * (p.at(i, j, k + 1) - p.at(i, j, k - 1));
            }
        }
    }
}

void project(SmokeState& s, const SmokeParams& params, SlabRange range, double* jacobiIterationMs) {
    // 1. Divergence of the current velocity (its ghost layer is up to date, see Stages.h invariant).
    computeDivergence(s.u, s.v, s.w, s.divergence, range);
    setBoundary(s.divergence, BoundaryKind::Scalar);  // as set_bnd(div) in Stam 2003

    // 2. Pressure: start from p = 0 every step, as in Stam 2003 (docs/plan/MATH.md, section 5), then Jacobi.
    //    Each iteration swaps the pair first, so it reads the previous iterate (pressure0) and writes the new
    //    one (pressure). The Neumann condition dp/dn = 0 at the walls (Fedkiw et al. 2001, eq. 4) is the
    //    Scalar boundary rule: the ghost cell copies its interior neighbour.
    s.pressure.fill(0.0f);
    auto iterate = [&] {
        for (int it = 0; it < params.jacobiIterations; ++it) {
            s.pressure.swap(s.pressure0);
            jacobiIteration(s.pressure, s.pressure0, s.divergence, range);
            setBoundary(s.pressure, BoundaryKind::Scalar);
        }
    };
    if (jacobiIterationMs && params.jacobiIterations > 0) {
        double totalMs = 0.0;
        {
            ScopedTimer timer(totalMs);
            iterate();
        }
        *jacobiIterationMs = totalMs / params.jacobiIterations;
    } else {
        if (jacobiIterationMs) *jacobiIterationMs = 0.0;
        iterate();
    }

    // 3. Make the velocity (nearly) divergence free: u -= grad p.
    subtractPressureGradient(s.u, s.v, s.w, s.pressure, range);
    setVelocityBoundary(s);
}

// ----- Boundary.cpp (73ceb9e) -----


// Stam 2003 (set_bnd) in 3D. The ghost layer is filled one axis at a time:
//   1. x walls (i = 0 and nx+1) for the interior rows j in [1,ny], k in [1,nz];
//   2. y walls (j = 0 and ny+1) for i in [0, nx+1], i.e. INCLUDING the ghost columns written in step 1;
//   3. z walls (k = 0 and nz+1) for i in [0, nx+1], j in [0, ny+1], i.e. including everything above.
// Because each pass also covers the ghost cells of the previous passes, the edges and corners of the ghost
// layer get a value by the same rule (copy or sign flip) as the faces. Stam's 2D code averages the corners
// instead; this deliberate difference is documented in docs/plan/MATH.md, section 6.
void setBoundary(Field3D& f, BoundaryKind kind) {
    const GridSize s = f.size();
    const int nx = s.nx, ny = s.ny, nz = s.nz;

    // -1 on the walls normal to the velocity component (no flow through the wall), +1 otherwise.
    const float signX = (kind == BoundaryKind::VelocityX) ? -1.0f : 1.0f;
    const float signY = (kind == BoundaryKind::VelocityY) ? -1.0f : 1.0f;
    const float signZ = (kind == BoundaryKind::VelocityZ) ? -1.0f : 1.0f;

    for (int k = 1; k <= nz; ++k) {
        for (int j = 1; j <= ny; ++j) {
            f.at(0, j, k)      = signX * f.at(1, j, k);
            f.at(nx + 1, j, k) = signX * f.at(nx, j, k);
        }
    }
    for (int k = 1; k <= nz; ++k) {
        for (int i = 0; i <= nx + 1; ++i) {
            f.at(i, 0, k)      = signY * f.at(i, 1, k);
            f.at(i, ny + 1, k) = signY * f.at(i, ny, k);
        }
    }
    for (int j = 0; j <= ny + 1; ++j) {
        for (int i = 0; i <= nx + 1; ++i) {
            f.at(i, j, 0)      = signZ * f.at(i, j, 1);
            f.at(i, j, nz + 1) = signZ * f.at(i, j, nz);
        }
    }
}

void setVelocityBoundary(SmokeState& state) {
    setBoundary(state.u, BoundaryKind::VelocityX);
    setBoundary(state.v, BoundaryKind::VelocityY);
    setBoundary(state.w, BoundaryKind::VelocityZ);
}

// ----- added for the demo: SmokeSolver::step of 73ceb9e, with the same per-stage timers -----
void m3Step(SmokeState& s, const SmokeParams& p, smoke::StepTimings& t) {
    using smoke::Stage;
    const float dt = p.dt;
    const SlabRange all = wholeInterior(s.density.size());
    t = smoke::StepTimings{};
    { ScopedTimer timer(t[Stage::Sources]);        addSources(s, p, dt, all); }
    { ScopedTimer timer(t[Stage::Forces]);         applyForces(s, p, dt, all); }
    { ScopedTimer timer(t[Stage::AdvectVelocity]); advectVelocity(s, dt, all); }
    { ScopedTimer timer(t[Stage::Project]);        project(s, p, all, &t.jacobiIterationMs); }
    { ScopedTimer timer(t[Stage::AdvectScalars]);  advectScalars(s, dt, all); }
}

} // namespace smoke_demo::SMOKE_M3_NS
