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
