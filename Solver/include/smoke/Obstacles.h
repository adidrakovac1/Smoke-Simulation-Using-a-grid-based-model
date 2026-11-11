#pragma once

#include "smoke/GridSize.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace smoke {

/// What a grid cell is made of (docs/plan/OBSTACLES.md).
///   Fluid   air and smoke move through it (the default);
///   Solid   part of an obstacle: no flow, no smoke;
///   Porous  air passes but is slowed down (e.g. a blanket); smoke can optionally be filtered.
enum class CellType : std::uint8_t { Fluid = 0, Solid = 1, Porous = 2 };

/// The type of every INTERIOR cell of the grid. Indices are the solver's interior indices:
/// i in [1, nx], j in [1, ny], k in [1, nz]. The walls of the box are not stored here; they are the ghost layer.
///
/// This is only a description. The solver copies it in SmokeSolver::setObstacles() and builds its own sparse
/// lists from it once, so changing an ObstacleGrid afterwards has no effect until setObstacles() is called again.
class ObstacleGrid {
public:
    ObstacleGrid() = default;
    explicit ObstacleGrid(const GridSize& size) { resize(size); }

    /// Reallocates for a new size; every cell becomes Fluid.
    void resize(const GridSize& size);
    /// Makes every cell Fluid (the size stays).
    void clear();

    const GridSize& size() const { return size_; }

    CellType at(int i, int j, int k) const { return static_cast<CellType>(cells_[index(i, j, k)]); }
    void     set(int i, int j, int k, CellType type) { cells_[index(i, j, k)] = static_cast<std::uint8_t>(type); }

    /// Number of cells of one type.
    std::size_t count(CellType type) const;
    /// True when every cell is Fluid.
    bool empty() const { return count(CellType::Fluid) == cells_.size(); }

    /// Resistance of the porous cells, sigma in 1/s: every step the velocity there is multiplied by
    /// 1 / (1 + dt * sigma). 0 = no resistance (the cell behaves like Fluid).
    float porousDrag = 0.0f;
    /// Smoke filtering of the porous cells, phi in 1/s: every step the density there is multiplied by
    /// 1 / (1 + dt * phi). 0 = smoke passes unchanged.
    float porousDensityFilter = 0.0f;

private:
    std::size_t index(int i, int j, int k) const {
        return static_cast<std::size_t>(i - 1) +
               static_cast<std::size_t>(size_.nx) *
                   (static_cast<std::size_t>(j - 1) + static_cast<std::size_t>(size_.ny) * static_cast<std::size_t>(k - 1));
    }

    GridSize                  size_{0, 0, 0};
    std::vector<std::uint8_t> cells_;
};

/// A triangle mesh: three floats per vertex, three vertex indices per triangle.
struct TriangleMesh {
    std::vector<float>         positions;
    std::vector<std::uint32_t> indices;

    std::size_t vertexCount() const { return positions.size() / 3; }
    std::size_t triangleCount() const { return indices.size() / 3; }
};

/// Smallest and largest coordinate of the mesh per axis. Returns false for a mesh without vertices.
bool meshBounds(const TriangleMesh& mesh, float outMin[3], float outMax[3]);

/// How a mesh becomes cells.
///   Solid   the surface and everything inside it (needs a closed mesh);
///   Shell   only the cells the surface passes through (thin things, open meshes);
///   Porous  like Solid, but the cells become Porous (an open sheet gives just its surface cells).
enum class ObstacleMode { Solid, Shell, Porous };

struct VoxelizeOptions {
    ObstacleMode mode = ObstacleMode::Solid;
    /// Half thickness of the surface layer in cells: a cell is marked when the surface passes within this
    /// distance of its centre along every axis. 0.5 marks exactly the cells the surface touches; 1.0 gives a
    /// layer at least two cells thick, which the pressure treatment needs for thin solid walls
    /// (docs/plan/OBSTACLES.md, "Poznata slabost").
    float shellHalfThickness = 0.5f;
};

struct VoxelizeStats {
    std::size_t surfaceCells  = 0;      ///< cells the surface passes through
    std::size_t interiorCells = 0;      ///< cells found inside by the flood fill (0 for Shell mode)
    bool        closed        = false;  ///< true when the flood fill found an inside
};

/// Marks the cells of `grid` covered by `mesh`. The mesh must be in BOX coordinates: x, y, z in [0, 1] along
/// each axis of the simulation box (the same convention as SmokeSource); parts outside the box are ignored.
/// Cells are added to what `grid` already contains, so several objects can be voxelized into one grid;
/// a Solid cell is never turned into Porous.
VoxelizeStats voxelize(const TriangleMesh& mesh, const VoxelizeOptions& options, ObstacleGrid& grid);

} // namespace smoke
