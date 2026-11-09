// The Jacobi kernel exactly as it was in M3 (commit 73ceb9e, Solver/src/stages/Projection.cpp), kept here
// as history so smoke_lab can show where M4 started. It is compiled twice (lab techniques T2 and T7):
//   jacobi_m3_scalar.cpp  with vectorization switched off  -> version 1 (like B0)
//   jacobi_m3_auto.cpp    with the default Release flags   -> version 2 (like B1)
// Include it inside an anonymous namespace; it needs smoke/Field3D.h.

void jacobiM3(smoke::Field3D& pNew, const smoke::Field3D& pOld, const smoke::Field3D& div) {
    const smoke::GridSize g = pNew.size();
    for (int k = 1; k <= g.nz; ++k) {
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
