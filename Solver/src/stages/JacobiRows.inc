// Jacobi slab kernel on raw pointers, shared by Projection.cpp (default flags) and Projection_avx2.cpp
// (compiled with -mavx2, technique T16). Include it ONLY inside an anonymous namespace, so each translation
// unit gets its own internal copy compiled with its own flags; that avoids the linker picking the AVX2 copy
// of a shared inline function for the whole program.
//
// It uses only raw pointers and integers (no Field3D), for the same reason. All fields have the same
// strides; row (j, k) starts at j * sJ + k * sK. Requires Restrict.h and <cstddef>.

void jacobiRow(float* SMOKE_RESTRICT pNew, const float* SMOKE_RESTRICT pOld, const float* SMOKE_RESTRICT div,
               int nx, std::ptrdiff_t sJ, std::ptrdiff_t sK) {
    for (int i = 1; i <= nx; ++i) {
        const float neighbours = pOld[i - 1] + pOld[i + 1] + pOld[i - sJ] + pOld[i + sJ] +
                                 pOld[i - sK] + pOld[i + sK];
        pNew[i] = (neighbours - div[i]) / 6.0f;
    }
}

void jacobiSlab(float* pNew, const float* pOld, const float* div, int nx, int ny, int k0, int k1,
                std::ptrdiff_t sJ, std::ptrdiff_t sK) {
    for (int k = k0; k < k1; ++k)
        for (int j = 1; j <= ny; ++j) {
            const std::ptrdiff_t row = j * sJ + k * sK;
            jacobiRow(pNew + row, pOld + row, div + row, nx, sJ, sK);
        }
}

// The FIRST Jacobi iteration of a step (D19). project() starts from p = 0 (pressure.fill(0)), so the six
// neighbours of every cell are +0.0 and their sum is +0.0. This row computes exactly that: (+0.0 - div) / 6,
// with the same subtraction and division as jacobiRow (0.0f - d is not folded to -d without -ffast-math,
// because the two differ for d = +0.0), so the result is bit-identical, and the old iterate is never read.
void jacobiRowFromZero(float* SMOKE_RESTRICT pNew, const float* SMOKE_RESTRICT div, int nx) {
    for (int i = 1; i <= nx; ++i) {
        const float neighbours = 0.0f;
        pNew[i] = (neighbours - div[i]) / 6.0f;
    }
}

// Rows [j0, j1) of plane k, interior cells only (used by the wavefront schedule, JacobiWavefront.cpp).
// fromZero: the first iteration of a step, see jacobiRowFromZero (pOld is then not read).
void jacobiPlaneRows(float* pNew, const float* pOld, const float* div, int nx, int j0, int j1, int k,
                     std::ptrdiff_t sJ, std::ptrdiff_t sK, bool fromZero) {
    for (int j = j0; j < j1; ++j) {
        const std::ptrdiff_t row = j * sJ + k * sK;
        if (fromZero)
            jacobiRowFromZero(pNew + row, div + row, nx);
        else
            jacobiRow(pNew + row, pOld + row, div + row, nx, sJ, sK);
    }
}
