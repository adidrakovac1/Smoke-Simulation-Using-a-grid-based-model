#pragma once

namespace smoke {

/// Read-only description of the density field that the renderer (or anything else) may read
/// without copying. This is the ONLY thing the Renderer-side code needs from the Solver.
///
///   origin          pointer to the first INTERIOR cell, i.e. interior cell (0,0,0)
///   nx, ny, nz      interior dimensions
///   strideJ         floats to skip for j+1 (one row)
///   strideK         floats to skip for k+1 (one plane)
///
/// value(i,j,k) = origin[i + strideJ*j + strideK*k], for 0 <= i < nx etc.
/// The row pitch is padded, so strideJ >= nx. To upload to a 3D texture either use
/// GL_UNPACK_ROW_LENGTH / GL_UNPACK_IMAGE_HEIGHT or copy into a packed buffer first.
struct DensityView {
    const float* origin = nullptr;
    int nx = 0, ny = 0, nz = 0;
    int strideJ = 0;
    int strideK = 0;
};

} // namespace smoke
