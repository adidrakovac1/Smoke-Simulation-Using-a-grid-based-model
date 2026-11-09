#include "stages/Stages.h"

namespace smoke::serial {

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

void project(SmokeState& s, const SmokeParams& params, SlabRange range) {
    // 1. Divergence of the current velocity (its ghost layer is up to date, see Stages.h invariant).
    computeDivergence(s.u, s.v, s.w, s.divergence, range);
    setBoundary(s.divergence, BoundaryKind::Scalar);  // as set_bnd(div) in Stam 2003

    // 2. Pressure: start from p = 0 every step, as in Stam 2003 (docs/plan/MATH.md, section 5), then Jacobi.
    //    Each iteration swaps the pair first, so it reads the previous iterate (pressure0) and writes the new
    //    one (pressure). The Neumann condition dp/dn = 0 at the walls (Fedkiw et al. 2001, eq. 4) is the
    //    Scalar boundary rule: the ghost cell copies its interior neighbour.
    s.pressure.fill(0.0f);
    for (int it = 0; it < params.jacobiIterations; ++it) {
        s.pressure.swap(s.pressure0);
        jacobiIteration(s.pressure, s.pressure0, s.divergence, range);
        setBoundary(s.pressure, BoundaryKind::Scalar);
    }

    // 3. Make the velocity (nearly) divergence free: u -= grad p.
    subtractPressureGradient(s.u, s.v, s.w, s.pressure, range);
    setVelocityBoundary(s);
}

} // namespace smoke::serial
