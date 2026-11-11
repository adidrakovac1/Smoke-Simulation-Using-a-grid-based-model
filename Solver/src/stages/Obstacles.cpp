#include "stages/ObstacleLists.h"
#include "stages/Stages.h"

#include <cstddef>

namespace smoke {

ObstacleLists buildObstacleLists(const ObstacleGrid& grid, const Field3D& layout) {
    ObstacleLists  lists;
    const GridSize g = grid.size();
    lists.porousDrag          = grid.porousDrag;
    lists.porousDensityFilter = grid.porousDensityFilter;
    lists.isSolid.assign(layout.totalFloats(), 0);
    lists.neighbourStart.push_back(0);

    auto isSolid = [&](int i, int j, int k) { return grid.at(i, j, k) == CellType::Solid; };
    for (int k = 1; k <= g.nz; ++k)
        for (int j = 1; j <= g.ny; ++j)
            for (int i = 1; i <= g.nx; ++i) {
                const CellType type = grid.at(i, j, k);
                if (type == CellType::Fluid) continue;
                const std::uint32_t cell = static_cast<std::uint32_t>(layout.index(i, j, k));
                if (i == 1 || j == 1 || k == 1 || i == g.nx || j == g.ny || k == g.nz) lists.touchesWall = true;
                if (type == CellType::Porous) {
                    lists.porous.push_back(cell);
                    continue;
                }
                lists.solid.push_back(cell);
                lists.isSolid[cell] = 1;

                // The six face neighbours that are inside the box and not solid.
                const int offsets[6][3] = {{-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}};
                std::uint32_t found = 0;
                for (const auto& o : offsets) {
                    const int ni = i + o[0], nj = j + o[1], nk = k + o[2];
                    if (ni < 1 || nj < 1 || nk < 1 || ni > g.nx || nj > g.ny || nk > g.nz) continue;
                    if (isSolid(ni, nj, nk)) continue;
                    lists.neighbour.push_back(static_cast<std::uint32_t>(layout.index(ni, nj, nk)));
                    lists.neighbourAxis.push_back(static_cast<std::uint8_t>(o[0] != 0 ? 0 : (o[1] != 0 ? 1 : 2)));
                    ++found;
                }
                if (found > 0) {
                    lists.boundarySolid.push_back(cell);
                    lists.neighbourStart.push_back(static_cast<std::uint32_t>(lists.neighbour.size()));
                    lists.neighbourWeight.push_back(1.0f / static_cast<float>(found));
                }
            }
    return lists;
}

} // namespace smoke

namespace smoke::serial {

// A wall of the box is handled by the ghost layer: the ghost cell copies its interior neighbour, so the
// difference across the wall is zero (setBoundary, Scalar). An obstacle surface is the same idea for a wall
// in the middle of the grid: the solid cell next to the fluid plays the role of the ghost cell. A solid cell
// can have several fluid neighbours (edges and corners of the obstacle), so it takes their mean.
void extendIntoSolids(Field3D& field, const ObstacleLists& obstacles) {
    float* data = field.data();
    for (std::size_t b = 0; b < obstacles.boundarySolid.size(); ++b) {
        float sum = 0.0f;
        for (std::uint32_t n = obstacles.neighbourStart[b]; n < obstacles.neighbourStart[b + 1]; ++n)
            sum += data[obstacles.neighbour[n]];
        data[obstacles.boundarySolid[b]] = sum * obstacles.neighbourWeight[b];
    }
}

void setInSolids(Field3D& field, float value, const ObstacleLists& obstacles) {
    float* data = field.data();
    for (const std::uint32_t cell : obstacles.solid) data[cell] = value;
}

// Velocity in solid cells: no flow through the obstacle surface.
//
// Deep inside an obstacle the velocity is simply 0. The solid cells NEXT TO the fluid get the same rule the
// ghost layer gets at the box walls (setBoundary, VelocityX/Y/Z; Stam 2003, set_bnd): the velocity component
// normal to the surface is the NEGATIVE of the fluid neighbour's, the other components are copied. The
// surface lies half-way between the two cells, so the normal velocity there, their mean, is exactly 0.
//
// Just writing 0 into those cells is not enough on this grid: the divergence is a central difference,
// ( u(i+1) - u(i-1) ) / 2, so a zero in solid cell i+1 would constrain cell i-1 and leave cell i, the one
// touching the obstacle, free to blow straight into it (measured: docs/plan/OBSTACLES.md, section 4).
//
// A solid cell with several fluid neighbours (edges, corners) takes the mean of what each neighbour asks for.
void reflectVelocityIntoSolids(Field3D& u, Field3D& v, Field3D& w, const ObstacleLists& obstacles) {
    setInSolids(u, 0.0f, obstacles);
    setInSolids(v, 0.0f, obstacles);
    setInSolids(w, 0.0f, obstacles);
    float* component[3] = {u.data(), v.data(), w.data()};
    for (std::size_t b = 0; b < obstacles.boundarySolid.size(); ++b) {
        float sum[3] = {0.0f, 0.0f, 0.0f};
        for (std::uint32_t n = obstacles.neighbourStart[b]; n < obstacles.neighbourStart[b + 1]; ++n) {
            const std::uint32_t from = obstacles.neighbour[n];
            const int           axis = obstacles.neighbourAxis[n];
            for (int c = 0; c < 3; ++c) sum[c] += (c == axis) ? -component[c][from] : component[c][from];
        }
        const std::uint32_t cell = obstacles.boundarySolid[b];
        for (int c = 0; c < 3; ++c) component[c][cell] = sum[c] * obstacles.neighbourWeight[b];
    }
}

void enforceSolidVelocity(SmokeState& s, const ObstacleLists& obstacles) {
    reflectVelocityIntoSolids(s.u, s.v, s.w, obstacles);
    if (obstacles.touchesWall) setVelocityBoundary(s);
}

// No smoke and no heat inside an obstacle.
void clearSolidScalars(SmokeState& s, float ambientTemperature, const ObstacleLists& obstacles) {
    setInSolids(s.density, 0.0f, obstacles);
    setInSolids(s.temperature, ambientTemperature, obstacles);
    if (obstacles.touchesWall) {
        setBoundary(s.density, BoundaryKind::Scalar);
        setBoundary(s.temperature, BoundaryKind::Scalar);
    }
}

// Porous cells resist the flow with a force proportional to the velocity, du/dt = -sigma * u. Solved
// implicitly over one step, u_new = u / (1 + dt * sigma), which can never overshoot or change the sign of u,
// whatever dt and sigma are.
void applyPorousDrag(SmokeState& s, float dt, const ObstacleLists& obstacles) {
    if (obstacles.porous.empty() || !(obstacles.porousDrag > 0.0f)) return;
    const float factor = 1.0f / (1.0f + dt * obstacles.porousDrag);
    float* u = s.u.data();
    float* v = s.v.data();
    float* w = s.w.data();
    for (const std::uint32_t cell : obstacles.porous) {
        u[cell] *= factor;
        v[cell] *= factor;
        w[cell] *= factor;
    }
    if (obstacles.touchesWall) setVelocityBoundary(s);
}

// Optional: porous cells keep part of the smoke that passes, d(rho)/dt = -phi * rho, same implicit form.
void applyPorousFilter(SmokeState& s, float dt, const ObstacleLists& obstacles) {
    if (obstacles.porous.empty() || !(obstacles.porousDensityFilter > 0.0f)) return;
    const float factor = 1.0f / (1.0f + dt * obstacles.porousDensityFilter);
    float* density = s.density.data();
    for (const std::uint32_t cell : obstacles.porous) density[cell] *= factor;
    if (obstacles.touchesWall) setBoundary(s.density, BoundaryKind::Scalar);
}

} // namespace smoke::serial
