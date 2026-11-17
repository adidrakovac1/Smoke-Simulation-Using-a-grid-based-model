// Red-black Gauss-Seidel / SOR row kernel on raw pointers (decision D20), shared by Projection.cpp (default flags)
// and Projection_avx2.cpp (-mavx2), like JacobiRows.inc. Include it ONLY inside an anonymous namespace.
// Requires Restrict.h and <cstddef>.
//
// One row (j, k) of one colour sweep, IN PLACE: the cells i with (i & 1) == odd are updated,
//     gs   = (sum of the 6 neighbours - div) / 6          (the Jacobi formula, MATH.md section 5)
//     p[i] = gs                    (Gauss-Seidel, Relax = false)
//     p[i] = p[i] + omega*(gs - p[i])   (SOR, Relax = true)
// the other cells keep their value. All 6 neighbours of an updated cell have the OTHER colour, so no cell of
// the sweep reads a value written in the same sweep, and the order of the cells does not matter.
//
// Vectorization (lab technique T8, #pragma omp simd): the loop computes the new value for EVERY cell of the row
// and then keeps it only for cells of the colour (a blend, no branch). Read in scalar order, cell i reads
// p[i - 1], which iteration i - 1 has just "written"; but iteration i - 1 either kept the old value (other
// colour) or cell i is of the other colour and its own result is thrown away. So the vector order gives exactly
// the same values as the scalar order, and the pragma's promise holds. Half of the arithmetic is wasted, but the
// sweep is limited by memory, not by arithmetic.
template <bool Relax>
SMOKE_NOINLINE void redBlackRow(float* SMOKE_RESTRICT p, const float* SMOKE_RESTRICT div, int nx, int odd, float omega,
                                std::ptrdiff_t sJ, std::ptrdiff_t sK) {
#pragma omp simd
    for (int i = 1; i <= nx; ++i) {
        const float neighbours = p[i - 1] + p[i + 1] + p[i - sJ] + p[i + sJ] + p[i - sK] + p[i + sK];
        const float gs = (neighbours - div[i]) / 6.0f;
        const float updated = Relax ? p[i] + omega * (gs - p[i]) : gs;
        p[i] = ((i & 1) == odd) ? updated : p[i];
    }
}

// One colour sweep over the planes [k0, k1): colour 0 = red ((i + j + k) even), 1 = black.
void redBlackSweep(float* p, const float* div, int nx, int ny, int k0, int k1, int colour, float omega,
                   std::ptrdiff_t sJ, std::ptrdiff_t sK) {
    const bool relax = omega != 1.0f;
    for (int k = k0; k < k1; ++k)
        for (int j = 1; j <= ny; ++j) {
            const std::ptrdiff_t row = j * sJ + k * sK;
            const int odd = (colour + j + k) & 1;  // (i + j + k) % 2 == colour  <=>  i % 2 == (colour + j + k) % 2
            if (relax)
                redBlackRow<true>(p + row, div + row, nx, odd, omega, sJ, sK);
            else
                redBlackRow<false>(p + row, div + row, nx, odd, omega, sJ, sK);
        }
}
