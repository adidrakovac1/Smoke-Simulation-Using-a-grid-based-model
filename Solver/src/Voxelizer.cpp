#include "smoke/Obstacles.h"

#include <algorithm>
#include <cmath>

namespace smoke {

// ─── ObstacleGrid ────────────────────────────────────────────────────────────

void ObstacleGrid::resize(const GridSize& size) {
    size_ = size;
    cells_.assign(size.valid() ? static_cast<std::size_t>(size.cells()) : 0, static_cast<std::uint8_t>(CellType::Fluid));
}

void ObstacleGrid::clear() { std::fill(cells_.begin(), cells_.end(), static_cast<std::uint8_t>(CellType::Fluid)); }

std::size_t ObstacleGrid::count(CellType type) const {
    return static_cast<std::size_t>(std::count(cells_.begin(), cells_.end(), static_cast<std::uint8_t>(type)));
}

bool meshBounds(const TriangleMesh& mesh, float outMin[3], float outMax[3]) {
    if (mesh.positions.size() < 3) return false;
    for (int axis = 0; axis < 3; ++axis) outMin[axis] = outMax[axis] = mesh.positions[static_cast<std::size_t>(axis)];
    for (std::size_t v = 0; v < mesh.vertexCount(); ++v)
        for (int axis = 0; axis < 3; ++axis) {
            const float value = mesh.positions[3 * v + static_cast<std::size_t>(axis)];
            outMin[axis]      = std::min(outMin[axis], value);
            outMax[axis]      = std::max(outMax[axis], value);
        }
    return true;
}

// ─── Triangle / box overlap ──────────────────────────────────────────────────

namespace {

struct Vec3 {
    double x, y, z;
};
Vec3   operator-(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
double dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3   cross(const Vec3& a, const Vec3& b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }

// Does the triangle (a, b, c) touch the cube with the given centre and half size?
//
// Separating axis theorem (Akenine-Moller, "Fast 3D Triangle-Box Overlap Testing", 2001): two convex shapes do
// NOT overlap exactly when there is an axis on which their projections do not overlap. For a triangle and a
// box it is enough to try 13 axes: the 3 box axes, the triangle normal, and the 9 cross products of a box axis
// with a triangle edge. If none of them separates the shapes, they overlap.
bool triangleTouchesBox(const Vec3& centre, double half, Vec3 a, Vec3 b, Vec3 c) {
    a = a - centre;  // work in a frame where the box is centred at the origin
    b = b - centre;
    c = c - centre;

    // The 3 box axes: compare the triangle's extent with [-half, half].
    if (std::min({a.x, b.x, c.x}) > half || std::max({a.x, b.x, c.x}) < -half) return false;
    if (std::min({a.y, b.y, c.y}) > half || std::max({a.y, b.y, c.y}) < -half) return false;
    if (std::min({a.z, b.z, c.z}) > half || std::max({a.z, b.z, c.z}) < -half) return false;

    const Vec3 edges[3] = {b - a, c - b, a - c};

    // The triangle normal: the box reaches at most `radius` along it; the triangle lies at distance |d|.
    const Vec3 normal = cross(edges[0], edges[1]);
    if (std::fabs(dot(normal, a)) > half * (std::fabs(normal.x) + std::fabs(normal.y) + std::fabs(normal.z))) return false;

    // The 9 cross products of a box axis with a triangle edge.
    const Vec3 boxAxes[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    for (const Vec3& edge : edges)
        for (const Vec3& boxAxis : boxAxes) {
            const Vec3   axis   = cross(boxAxis, edge);
            const double pa     = dot(axis, a), pb = dot(axis, b), pc = dot(axis, c);
            const double radius = half * (std::fabs(axis.x) + std::fabs(axis.y) + std::fabs(axis.z));
            if (std::min({pa, pb, pc}) > radius || std::max({pa, pb, pc}) < -radius) return false;
        }
    return true;
}

} // namespace

// ─── Voxelization ────────────────────────────────────────────────────────────

VoxelizeStats voxelize(const TriangleMesh& mesh, const VoxelizeOptions& options, ObstacleGrid& grid) {
    VoxelizeStats  stats;
    const GridSize g = grid.size();
    if (!g.valid() || mesh.triangleCount() == 0) return stats;

    // Work array with one extra layer of cells around the interior (indices 0 .. n + 1). The extra layer is
    // "certainly outside" and is where the flood fill starts.
    const std::size_t sx = static_cast<std::size_t>(g.nx) + 2, sy = static_cast<std::size_t>(g.ny) + 2,
                      sz = static_cast<std::size_t>(g.nz) + 2;
    auto index = [sx, sy](int i, int j, int k) {
        return static_cast<std::size_t>(i) + sx * (static_cast<std::size_t>(j) + sy * static_cast<std::size_t>(k));
    };
    enum : std::uint8_t { kUnknown = 0, kSurface = 1, kOutside = 2 };
    std::vector<std::uint8_t> mark(sx * sy * sz, kUnknown);

    // 1. Surface: every cell a triangle passes through. Box coordinate x in [0,1] becomes the cell coordinate
    //    0.5 + x * nx, in which cell i is the cube [i - 0.5, i + 0.5] (the convention of Sources.cpp).
    const double half = std::max(0.5, static_cast<double>(options.shellHalfThickness));
    const double scale[3] = {static_cast<double>(g.nx), static_cast<double>(g.ny), static_cast<double>(g.nz)};
    const int    last[3]  = {g.nx, g.ny, g.nz};
    for (std::size_t t = 0; t < mesh.triangleCount(); ++t) {
        Vec3 corner[3];
        for (int c = 0; c < 3; ++c) {
            const std::size_t v = static_cast<std::size_t>(mesh.indices[3 * t + static_cast<std::size_t>(c)]);
            if (v >= mesh.vertexCount()) return stats;  // broken mesh: do nothing rather than read outside the array
            corner[c] = {0.5 + mesh.positions[3 * v] * scale[0], 0.5 + mesh.positions[3 * v + 1] * scale[1],
                         0.5 + mesh.positions[3 * v + 2] * scale[2]};
        }
        // Only the cells inside the triangle's bounding box (grown by `half`) can touch it.
        const double lo[3] = {std::min({corner[0].x, corner[1].x, corner[2].x}), std::min({corner[0].y, corner[1].y, corner[2].y}),
                              std::min({corner[0].z, corner[1].z, corner[2].z})};
        const double hi[3] = {std::max({corner[0].x, corner[1].x, corner[2].x}), std::max({corner[0].y, corner[1].y, corner[2].y}),
                              std::max({corner[0].z, corner[1].z, corner[2].z})};
        int first[3], end[3];
        bool outsideBox = false;
        for (int axis = 0; axis < 3; ++axis) {
            if (hi[axis] + half < 0.5 || lo[axis] - half > last[axis] + 0.5) outsideBox = true;
            first[axis] = std::max(1, static_cast<int>(std::ceil(std::max(lo[axis] - half, -1.0e6))));
            end[axis]   = std::min(last[axis], static_cast<int>(std::floor(std::min(hi[axis] + half, 1.0e6))));
        }
        if (outsideBox) continue;
        for (int k = first[2]; k <= end[2]; ++k)
            for (int j = first[1]; j <= end[1]; ++j)
                for (int i = first[0]; i <= end[0]; ++i) {
                    std::uint8_t& cell = mark[index(i, j, k)];
                    if (cell == kSurface) continue;
                    const Vec3 centre{static_cast<double>(i), static_cast<double>(j), static_cast<double>(k)};
                    if (triangleTouchesBox(centre, half, corner[0], corner[1], corner[2])) {
                        cell = kSurface;
                        ++stats.surfaceCells;
                    }
                }
    }

    // 2. Inside: flood fill from the outer layer through everything that is not surface. Whatever the flood
    //    cannot reach is enclosed by the surface. If the mesh has a hole, the flood leaks in and nothing is inside.
    if (options.mode != ObstacleMode::Shell) {
        std::vector<std::size_t> stack;
        auto visit = [&](int i, int j, int k) {
            std::uint8_t& cell = mark[index(i, j, k)];
            if (cell == kUnknown) {
                cell = kOutside;
                stack.push_back(index(i, j, k));
            }
        };
        for (int k = 0; k <= g.nz + 1; ++k)
            for (int j = 0; j <= g.ny + 1; ++j)
                for (int i = 0; i <= g.nx + 1; ++i)
                    if (i == 0 || j == 0 || k == 0 || i == g.nx + 1 || j == g.ny + 1 || k == g.nz + 1) visit(i, j, k);
        while (!stack.empty()) {
            const std::size_t current = stack.back();
            stack.pop_back();
            const int i = static_cast<int>(current % sx);
            const int j = static_cast<int>((current / sx) % sy);
            const int k = static_cast<int>(current / (sx * sy));
            if (i > 0) visit(i - 1, j, k);
            if (i < g.nx + 1) visit(i + 1, j, k);
            if (j > 0) visit(i, j - 1, k);
            if (j < g.ny + 1) visit(i, j + 1, k);
            if (k > 0) visit(i, j, k - 1);
            if (k < g.nz + 1) visit(i, j, k + 1);
        }
    }

    // 3. Write the result into the grid.
    const CellType type = options.mode == ObstacleMode::Porous ? CellType::Porous : CellType::Solid;
    for (int k = 1; k <= g.nz; ++k)
        for (int j = 1; j <= g.ny; ++j)
            for (int i = 1; i <= g.nx; ++i) {
                const std::uint8_t cell   = mark[index(i, j, k)];
                const bool         inside = options.mode != ObstacleMode::Shell && cell == kUnknown;
                if (inside) ++stats.interiorCells;
                if (cell != kSurface && !inside) continue;
                if (grid.at(i, j, k) == CellType::Solid) continue;  // an earlier solid object stays solid
                grid.set(i, j, k, type);
            }
    stats.closed = stats.interiorCells > 0;
    return stats;
}

} // namespace smoke
