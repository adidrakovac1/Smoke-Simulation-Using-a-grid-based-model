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

// Lab technique T3 (loop unrolling): the same M3 code, but the i loop does 4 cells per pass, plus a remainder
// loop. Each cell is computed with exactly the same expression, so the result is bit-identical. Exported only
// from jacobi_m3_scalar.cpp (no vectorization), to show what unrolling alone gives to scalar code.
[[maybe_unused]] void jacobiM3Unrolled(smoke::Field3D& pNew, const smoke::Field3D& pOld, const smoke::Field3D& div) {
    const smoke::GridSize g = pNew.size();
    auto cell = [&](int i, int j, int k) {
        const float neighbours = pOld.at(i - 1, j, k) + pOld.at(i + 1, j, k) +
                                 pOld.at(i, j - 1, k) + pOld.at(i, j + 1, k) +
                                 pOld.at(i, j, k - 1) + pOld.at(i, j, k + 1);
        pNew.at(i, j, k) = (neighbours - div.at(i, j, k)) / 6.0f;
    };
    for (int k = 1; k <= g.nz; ++k) {
        for (int j = 1; j <= g.ny; ++j) {
            int i = 1;
            for (; i + 3 <= g.nx; i += 4) {
                cell(i, j, k);
                cell(i + 1, j, k);
                cell(i + 2, j, k);
                cell(i + 3, j, k);
            }
            for (; i <= g.nx; ++i) cell(i, j, k);
        }
    }
}
