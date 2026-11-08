#pragma once

namespace smoke {

/// Number of INTERIOR cells per axis. The grid size is a runtime value, not a compile-time
/// constant: default 64^3, but 32^3, 96^3, 128^3 or non-cubic grids (e.g. 64x128x64) are all valid.
/// The solver additionally keeps a one-cell "ghost" layer around the interior for boundary values.
struct GridSize {
    int nx = 64;
    int ny = 64;
    int nz = 64;

    constexpr long long cells() const { return 1LL * nx * ny * nz; }
    constexpr bool valid() const { return nx > 0 && ny > 0 && nz > 0; }

    friend constexpr bool operator==(const GridSize& a, const GridSize& b) {
        return a.nx == b.nx && a.ny == b.ny && a.nz == b.nz;
    }
};

} // namespace smoke
