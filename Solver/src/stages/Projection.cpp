#include "stages/Stages.h"

#include "CpuFeatures.h"
#include "Profiler.h"
#include "Restrict.h"

#include <cassert>
#include <cstddef>

namespace smoke::serial {

// Pressure projection (Stam 1999, appendix B; Stam 2003, project(); docs/plan/MATH.md, section 5).
//
// Incompressible air must have zero divergence: as much flows into a cell as flows out. After forces and
// advection that is no longer true. We find a pressure p with  laplacian(p) = div(u)  and subtract its
// gradient, which removes (most of) the divergence. Sign check against Stam 2003: there
// div_S = -0.5*h*(...) and p = (div_S + sum)/4; our div has the opposite sign, so p = (sum - div)/6 is the
// same equation in 3D. Compared with Fedkiw et al. 2001, eq. (4)-(5), our p is dt * p (MATH.md, section 5).

// T12 (M4-007): the innermost loop of each kernel is a row function whose pointer PARAMETERS are marked
// SMOKE_RESTRICT (GCC honours restrict reliably on parameters, not on local pointers). All fields of one
// SmokeState have the same GridSize and therefore the same strides (checked by assert), so one row offset
// serves every field. The fields passed by project() are always distinct arrays.
namespace {

void divergenceRow(float* SMOKE_RESTRICT d, const float* SMOKE_RESTRICT u, const float* SMOKE_RESTRICT v,
                   const float* SMOKE_RESTRICT w, int nx, std::ptrdiff_t sJ, std::ptrdiff_t sK) {
    for (int i = 1; i <= nx; ++i)
        d[i] = 0.5f * (u[i + 1] - u[i - 1] + v[i + sJ] - v[i - sJ] + w[i + sK] - w[i - sK]);
}

// jacobiRow / jacobiSlab: the same source is compiled again with -mavx2 in Projection_avx2.cpp (M4-009).
#include "stages/JacobiRows.inc"

void gradientRow(float* SMOKE_RESTRICT u, float* SMOKE_RESTRICT v, float* SMOKE_RESTRICT w,
                 const float* SMOKE_RESTRICT p, int nx, std::ptrdiff_t sJ, std::ptrdiff_t sK) {
    for (int i = 1; i <= nx; ++i) {
        u[i] -= 0.5f * (p[i + 1] - p[i - 1]);
        v[i] -= 0.5f * (p[i + sJ] - p[i - sJ]);
        w[i] -= 0.5f * (p[i + sK] - p[i - sK]);
    }
}

} // namespace

void computeDivergence(const Field3D& u, const Field3D& v, const Field3D& w, Field3D& div, SlabRange range) {
    const GridSize g = div.size();
    const std::ptrdiff_t sJ = div.strideJ(), sK = div.strideK();
    assert(u.strideK() == sK && v.strideK() == sK && w.strideK() == sK);
    for (int k = range.k0; k < range.k1; ++k)
        for (int j = 1; j <= g.ny; ++j) {
            const std::size_t row = div.index(0, j, k);
            divergenceRow(div.data() + row, u.data() + row, v.data() + row, w.data() + row, g.nx, sJ, sK);
        }
}

#if defined(SMOKE_HAS_AVX2_BINARY)
// Defined in Projection_avx2.cpp (compiled with -mavx2). Only call it when avx2Supported() is true.
void jacobiSlabAvx2(float* pNew, const float* pOld, const float* div, int nx, int ny, int k0, int k1,
                    std::ptrdiff_t sJ, std::ptrdiff_t sK);
#endif

bool jacobiIteration(Field3D& pNew, const Field3D& pOld, const Field3D& div, SlabRange range, KernelIsa isa) {
    const GridSize g = pNew.size();
    const std::ptrdiff_t sJ = pNew.strideJ(), sK = pNew.strideK();
    assert(pOld.strideK() == sK && div.strideK() == sK);
    const bool haveAvx2 = avx2Supported();  // always false when the AVX2 file is not built
    if (isa == KernelIsa::Avx2 && !haveAvx2) return false;
#if defined(SMOKE_HAS_AVX2_BINARY)
    if (isa != KernelIsa::Scalar && haveAvx2) {
        jacobiSlabAvx2(pNew.data(), pOld.data(), div.data(), g.nx, g.ny, range.k0, range.k1, sJ, sK);
        return true;
    }
#endif
    jacobiSlab(pNew.data(), pOld.data(), div.data(), g.nx, g.ny, range.k0, range.k1, sJ, sK);
    return true;
}

void subtractPressureGradient(Field3D& u, Field3D& v, Field3D& w, const Field3D& p, SlabRange range) {
    const GridSize g = p.size();
    const std::ptrdiff_t sJ = p.strideJ(), sK = p.strideK();
    assert(u.strideK() == sK && v.strideK() == sK && w.strideK() == sK);
    for (int k = range.k0; k < range.k1; ++k)
        for (int j = 1; j <= g.ny; ++j) {
            const std::size_t row = p.index(0, j, k);
            gradientRow(u.data() + row, v.data() + row, w.data() + row, p.data() + row, g.nx, sJ, sK);
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

} // namespace smoke::serial
