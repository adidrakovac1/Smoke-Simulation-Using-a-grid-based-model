#pragma once

// Obstacle scenarios shared by the measuring programs (smoke_bench --obstacle, smoke_prof --obstacle), so the
// cost measurement (docs/plan/PERF_LOG.md, section 9) and the profile (docs/plan/PROFILE.md, section 6) look at
// exactly the same obstacles.
//
// The cells are marked directly from a formula, without a mesh, so the scenario is the same on every run and
// scales with the grid. Positions follow the renderer demo (Integration/src/SmokeScene.cpp):
//   sphere   solid ball, centre (0.5, 0.5, 0.5), radius 0.15 of the box, above the smoke source;
//   blanket  porous horizontal layer two cells thick at height 0.45, 0.7 x 0.7 of the box, sigma = 20 1/s.
// A cell belongs to the obstacle when its centre does.

#include "smoke/GridSize.h"
#include "smoke/Obstacles.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace smoke::scenarios {

inline bool isObstacleScenario(const std::string& name) {
    return name == "none" || name == "sphere" || name == "blanket";
}

/// Returns an all-Fluid grid for "none" (and for an unknown name).
inline ObstacleGrid makeObstacleScenario(const std::string& name, const GridSize& size) {
    ObstacleGrid grid(size);
    if (name == "sphere") {
        constexpr double radius = 0.15;
        for (int k = 1; k <= size.nz; ++k)
            for (int j = 1; j <= size.ny; ++j)
                for (int i = 1; i <= size.nx; ++i) {
                    const double x = (i - 0.5) / size.nx - 0.5;
                    const double y = (j - 0.5) / size.ny - 0.5;
                    const double z = (k - 0.5) / size.nz - 0.5;
                    if (x * x + y * y + z * z <= radius * radius) grid.set(i, j, k, CellType::Solid);
                }
    } else if (name == "blanket") {
        grid.porousDrag = 20.0f;  // the default of the renderer panel; a tuning value, not a measured material
        const int firstLayer = static_cast<int>(0.45 * size.ny) + 1;
        for (int k = 1; k <= size.nz; ++k)
            for (int j = firstLayer; j <= std::min(firstLayer + 1, size.ny); ++j)
                for (int i = 1; i <= size.nx; ++i) {
                    const double x = (i - 0.5) / size.nx - 0.5;
                    const double z = (k - 0.5) / size.nz - 0.5;
                    if (std::fabs(x) <= 0.35 && std::fabs(z) <= 0.35) grid.set(i, j, k, CellType::Porous);
                }
    }
    return grid;
}

}  // namespace smoke::scenarios
