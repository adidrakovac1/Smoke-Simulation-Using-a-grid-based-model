#pragma once

// Obstacles inside the box, as SPARSE LISTS (decision D16, docs/plan/OBSTACLES.md).
//
// The vectorized kernels (Jacobi, advection, forces) know nothing about obstacles and still run over every
// cell. Obstacles are enforced afterwards, by short loops over the few cells that are solid or porous. So a
// simulation without obstacles does no extra work per cell, and the kernels stay exactly as measured in M4.

#include "smoke/Field3D.h"
#include "smoke/Obstacles.h"

#include <cstdint>
#include <vector>

namespace smoke {

/// Built once from an ObstacleGrid (SmokeSolver::setObstacles). All cell numbers are indices into the float
/// array of a Field3D (Field3D::index), so they can be used with every field of the same grid.
struct ObstacleLists {
    /// Every solid cell.
    std::vector<std::uint32_t> solid;

    /// Solid cells that touch at least one non-solid interior cell: the surface of the obstacles as seen from
    /// the fluid. For boundary cell b, its non-solid neighbours are neighbour[neighbourStart[b] ..
    /// neighbourStart[b + 1]) and neighbourWeight[b] is 1 / (their number).
    std::vector<std::uint32_t> boundarySolid;
    std::vector<std::uint32_t> neighbourStart;
    std::vector<std::uint32_t> neighbour;
    std::vector<std::uint8_t>  neighbourAxis;  ///< 0, 1 or 2: the axis along which that neighbour lies (x, y, z)
    std::vector<float>         neighbourWeight;

    /// Every porous cell, with the two coefficients of ObstacleGrid.
    std::vector<std::uint32_t> porous;
    float                      porousDrag          = 0.0f;
    float                      porousDensityFilter = 0.0f;

    /// 1 for solid cells, 0 otherwise; one byte per float of the field layout. Used by the diagnostics.
    std::vector<std::uint8_t> isSolid;

    /// True when an obstacle cell lies next to a wall of the box; then the ghost layer has to be refreshed
    /// after the obstacle cells are changed.
    bool touchesWall = false;

    bool empty() const { return solid.empty() && porous.empty(); }
};

/// `layout` is any field of the grid; only its size and strides are used.
ObstacleLists buildObstacleLists(const ObstacleGrid& grid, const Field3D& layout);

} // namespace smoke
