// Obstacles (M-OBS, docs/plan/OBSTACLES.md): .obj parser, voxelization, sparse lists and their effect on the
// simulation. Measured numbers are printed with MESSAGE; the limits next to them say when they were measured.
#include "doctest/doctest.h"
#include "smoke/ObjLoader.h"
#include "smoke/Obstacles.h"
#include "smoke/SmokeSolver.h"
#include "stages/ObstacleLists.h"
#include "stages/Stages.h"
#include "test_helpers.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <sstream>

using smoke::CellType;
using smoke::GridSize;
using smoke::ObstacleGrid;
using smoke::ObstacleMode;
using smoke::SmokeSolver;
using smoke::SmokeState;
using smoke::SolverConfig;
using smoke::TriangleMesh;
using namespace smoke::serial;
using namespace smoke_test;

namespace {

// Axis-aligned box in box coordinates, 12 triangles. `skipTop` leaves the +y face out (an open mesh).
TriangleMesh makeBox(float x0, float y0, float z0, float x1, float y1, float z1, bool skipTop = false) {
    TriangleMesh m;
    m.positions = {x0, y0, z0, x1, y0, z0, x1, y1, z0, x0, y1, z0, x0, y0, z1, x1, y0, z1, x1, y1, z1, x0, y1, z1};
    const std::uint32_t quads[6][4] = {{0, 1, 2, 3}, {4, 5, 6, 7}, {0, 1, 5, 4}, {3, 2, 6, 7}, {0, 3, 7, 4}, {1, 2, 6, 5}};
    for (int q = 0; q < 6; ++q) {
        if (skipTop && q == 3) continue;
        m.indices.insert(m.indices.end(), {quads[q][0], quads[q][1], quads[q][2], quads[q][0], quads[q][2], quads[q][3]});
    }
    return m;
}

// Sphere made of latitude / longitude bands (closed).
TriangleMesh makeSphere(float cx, float cy, float cz, float radius, int stacks = 16, int slices = 32) {
    TriangleMesh m;
    const float  pi = 3.14159265358979323846f;
    for (int s = 0; s <= stacks; ++s) {
        const float phi = pi * static_cast<float>(s) / static_cast<float>(stacks);
        for (int t = 0; t <= slices; ++t) {
            const float theta = 2.0f * pi * static_cast<float>(t) / static_cast<float>(slices);
            m.positions.insert(m.positions.end(), {cx + radius * std::sin(phi) * std::cos(theta), cy + radius * std::cos(phi),
                                                   cz + radius * std::sin(phi) * std::sin(theta)});
        }
    }
    const std::uint32_t ring = static_cast<std::uint32_t>(slices) + 1;
    for (std::uint32_t s = 0; s < static_cast<std::uint32_t>(stacks); ++s)
        for (std::uint32_t t = 0; t < static_cast<std::uint32_t>(slices); ++t) {
            const std::uint32_t a = s * ring + t, b = a + ring;
            m.indices.insert(m.indices.end(), {a, a + 1, b, a + 1, b + 1, b});
        }
    return m;
}

// Density of interior cell (i, j, k), 1-based like the solver, read through the public view.
float densityAt(const SmokeSolver& solver, int i, int j, int k) {
    const smoke::DensityView v = solver.density();
    return v.origin[(i - 1) + 1LL * v.strideJ * (j - 1) + 1LL * v.strideK * (k - 1)];
}

// Number of interior cells whose density differs bit for bit between two solvers.
long long densityCellsDifferent(const SmokeSolver& a, const SmokeSolver& b) {
    const GridSize g = a.gridSize();
    long long      n = 0;
    for (int k = 1; k <= g.nz; ++k)
        for (int j = 1; j <= g.ny; ++j)
            for (int i = 1; i <= g.nx; ++i) {
                const float x = densityAt(a, i, j, k), y = densityAt(b, i, j, k);
                n += std::memcmp(&x, &y, sizeof(float)) != 0;
            }
    return n;
}

// FNV-1a over the density bytes: one number that changes if any bit changes. Printed so that the builds with
// and without AVX2 kernels can be compared by eye.
std::uint64_t densityHash(const SmokeSolver& solver) {
    const GridSize g    = solver.gridSize();
    std::uint64_t  hash = 1469598103934665603ull;
    for (int k = 1; k <= g.nz; ++k)
        for (int j = 1; j <= g.ny; ++j)
            for (int i = 1; i <= g.nx; ++i) {
                const float   value = densityAt(solver, i, j, k);
                unsigned char bytes[sizeof(float)];
                std::memcpy(bytes, &value, sizeof(float));
                for (unsigned char byte : bytes) hash = (hash ^ byte) * 1099511628211ull;
            }
    return hash;
}

double massInRows(const SmokeSolver& solver, int j0, int j1) {
    const GridSize g = solver.gridSize();
    double         m = 0.0;
    for (int k = 1; k <= g.nz; ++k)
        for (int j = j0; j <= j1; ++j)
            for (int i = 1; i <= g.nx; ++i) m += densityAt(solver, i, j, k);
    return m;
}

} // namespace

TEST_CASE("obj_parser_reads_vertices_polygons_slash_forms_and_negative_indices") {
    std::istringstream text("# a square and a triangle\n"
                            "o thing\n"
                            "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\n"
                            "vn 0 0 1\nvt 0 0\n"
                            "f 1/1/1 2/1/1 3/1/1 4/1/1\n"   // quad -> 2 triangles
                            "v 0.5 2 0\n"
                            "f -3//1 -2//1 -1\n");           // relative: vertices 3, 4, 5
    TriangleMesh mesh;
    std::string  error;
    REQUIRE(smoke::parseObj(text, mesh, &error));
    CHECK(mesh.vertexCount() == 5);
    REQUIRE(mesh.triangleCount() == 3);
    const std::vector<std::uint32_t> expected = {0, 1, 2, 0, 2, 3, 2, 3, 4};
    CHECK(mesh.indices == expected);
    CHECK(mesh.positions[3 * 4 + 1] == 2.0f);

    float lo[3], hi[3];
    REQUIRE(smoke::meshBounds(mesh, lo, hi));
    CHECK(lo[0] == 0.0f);
    CHECK(hi[1] == 2.0f);
}

TEST_CASE("obj_parser_rejects_unusable_text_with_a_line_number") {
    TriangleMesh mesh;
    std::string  error;
    std::istringstream missingVertex("v 0 0 0\nv 1 0 0\nf 1 2 3\n");
    CHECK_FALSE(smoke::parseObj(missingVertex, mesh, &error));
    CHECK(error.find("line 3") != std::string::npos);

    std::istringstream noFaces("v 0 0 0\nv 1 0 0\nv 0 1 0\n");
    CHECK_FALSE(smoke::parseObj(noFaces, mesh, &error));

    CHECK_FALSE(smoke::loadObj("this/file/does/not/exist.obj", mesh, &error));
}

TEST_CASE("voxelized_box_and_sphere_volumes_are_close_to_the_analytic_ones") {
    const GridSize g{32, 32, 32};

    // Box 0.26 .. 0.74: 15.36 cells per side. Voxelization is conservative (every cell the surface touches is
    // marked), so the count can only be larger than the true volume, by about one layer of cells.
    ObstacleGrid boxGrid(g);
    const smoke::VoxelizeStats boxStats = smoke::voxelize(makeBox(0.26f, 0.26f, 0.26f, 0.74f, 0.74f, 0.74f), {}, boxGrid);
    const double boxCells    = static_cast<double>(boxGrid.count(CellType::Solid));
    const double boxAnalytic = 15.36 * 15.36 * 15.36;
    MESSAGE("box: " << boxCells << " solid cells (surface " << boxStats.surfaceCells << ", inside " << boxStats.interiorCells
                    << "), analytic volume " << boxAnalytic << " cells, ratio " << boxCells / boxAnalytic);
    CHECK(boxStats.closed);
    CHECK(boxCells == 16.0 * 16.0 * 16.0);  // cells 9 .. 24 on every axis overlap [8.82, 24.18]
    CHECK(boxGrid.count(CellType::Porous) == 0);

    // Sphere of radius 0.25 = 8 cells.
    ObstacleGrid sphereGrid(g);
    const smoke::VoxelizeStats sphereStats = smoke::voxelize(makeSphere(0.5f, 0.5f, 0.5f, 0.25f), {}, sphereGrid);
    const double sphereCells    = static_cast<double>(sphereGrid.count(CellType::Solid));
    const double sphereAnalytic = 4.0 / 3.0 * 3.14159265358979323846 * 8.0 * 8.0 * 8.0;
    MESSAGE("sphere: " << sphereCells << " solid cells, analytic volume " << sphereAnalytic << " cells, ratio "
                       << sphereCells / sphereAnalytic);
    CHECK(sphereStats.closed);
    // Measured 2026-11-11 (32^3, r = 8 cells, g++ 16.2.0): 2750 cells, ratio 1.28, i.e. about 0.7 cell of extra
    // radius, as expected for a conservative surface layer. Limits: never smaller than the true volume, and less than
    // one and a half cells of extra radius ((9.5 / 8)^3 = 1.67). A missing flood fill gives a ratio near 0.4.
    CHECK(sphereCells >= sphereAnalytic);
    CHECK(sphereCells < 1.67 * sphereAnalytic);

    // Mirror symmetry of the voxelized sphere in x (the mesh and the grid are symmetric about the middle).
    long long asymmetric = 0;
    for (int k = 1; k <= g.nz; ++k)
        for (int j = 1; j <= g.ny; ++j)
            for (int i = 1; i <= g.nx; ++i) asymmetric += sphereGrid.at(i, j, k) != sphereGrid.at(g.nx + 1 - i, j, k);
    MESSAGE("sphere: cells that differ from their mirror image in x: " << asymmetric);
}

TEST_CASE("flood_fill_finds_the_inside_only_for_a_closed_mesh") {
    const GridSize g{24, 24, 24};

    ObstacleGrid closed(g);
    const smoke::VoxelizeStats closedStats = smoke::voxelize(makeBox(0.3f, 0.3f, 0.3f, 0.7f, 0.7f, 0.7f), {}, closed);
    CHECK(closedStats.closed);
    CHECK(closedStats.interiorCells > 0);
    CHECK(closed.at(12, 12, 12) == CellType::Solid);  // the centre is inside
    CHECK(closed.at(2, 2, 2) == CellType::Fluid);

    // The same box without its top: the flood fill enters, so only the surface cells remain.
    ObstacleGrid open(g);
    const smoke::VoxelizeStats openStats =
        smoke::voxelize(makeBox(0.3f, 0.3f, 0.3f, 0.7f, 0.7f, 0.7f, /*skipTop=*/true), {}, open);
    CHECK_FALSE(openStats.closed);
    CHECK(open.at(12, 12, 12) == CellType::Fluid);
    CHECK(open.count(CellType::Solid) == openStats.surfaceCells);

    // Shell mode never fills, even for a closed mesh; Porous mode marks Porous cells.
    ObstacleGrid shell(g);
    smoke::VoxelizeOptions shellOptions;
    shellOptions.mode = ObstacleMode::Shell;
    smoke::voxelize(makeBox(0.3f, 0.3f, 0.3f, 0.7f, 0.7f, 0.7f), shellOptions, shell);
    CHECK(shell.at(12, 12, 12) == CellType::Fluid);
    CHECK(shell.count(CellType::Solid) == closedStats.surfaceCells);

    // A thicker shell: half thickness 1.0 marks at least two cells across a flat sheet.
    ObstacleGrid thin(g), thick(g);
    TriangleMesh sheet;
    sheet.positions = {0.2f, 0.52f, 0.2f, 0.8f, 0.52f, 0.2f, 0.8f, 0.52f, 0.8f, 0.2f, 0.52f, 0.8f};
    sheet.indices   = {0, 1, 2, 0, 2, 3};
    smoke::VoxelizeOptions thickOptions = shellOptions;
    thickOptions.shellHalfThickness     = 1.0f;
    smoke::voxelize(sheet, shellOptions, thin);
    smoke::voxelize(sheet, thickOptions, thick);
    int thinLayers = 0, thickLayers = 0;
    for (int j = 1; j <= g.ny; ++j) {
        thinLayers += thin.at(12, j, 12) == CellType::Solid;
        thickLayers += thick.at(12, j, 12) == CellType::Solid;
    }
    MESSAGE("flat sheet at y = 0.52 on 24^3: layers marked with half thickness 0.5 / 1.0: " << thinLayers << " / " << thickLayers);
    CHECK(thinLayers >= 1);
    CHECK(thickLayers >= 2);

    ObstacleGrid porous(g);
    smoke::VoxelizeOptions porousOptions;
    porousOptions.mode = ObstacleMode::Porous;
    smoke::voxelize(sheet, porousOptions, porous);
    CHECK(porous.count(CellType::Solid) == 0);
    CHECK(porous.count(CellType::Porous) > 0);
}

TEST_CASE("obstacle_lists_hold_the_solid_surface_and_its_fluid_neighbours") {
    const GridSize g{8, 8, 8};
    smoke::Field3D layout(g);
    ObstacleGrid   grid(g);
    // A 2 x 1 x 1 bar in the middle and one porous cell elsewhere.
    grid.set(4, 4, 4, CellType::Solid);
    grid.set(5, 4, 4, CellType::Solid);
    grid.set(2, 2, 2, CellType::Porous);

    const smoke::ObstacleLists lists = smoke::buildObstacleLists(grid, layout);
    CHECK(lists.solid.size() == 2);
    CHECK(lists.boundarySolid.size() == 2);
    CHECK(lists.porous.size() == 1);
    CHECK(lists.neighbour.size() == 10);  // each bar cell has 5 non-solid neighbours
    CHECK(lists.neighbourAxis.size() == 10);
    CHECK(lists.neighbourAxis[0] == 0);   // the first neighbour of (4,4,4) is (3,4,4): along x
    CHECK(lists.neighbourWeight[0] == doctest::Approx(0.2f));
    CHECK_FALSE(lists.touchesWall);
    CHECK(lists.isSolid[layout.index(4, 4, 4)] == 1);
    CHECK(lists.isSolid[layout.index(2, 2, 2)] == 0);

    // extendIntoSolids: the solid cell becomes the mean of its non-solid neighbours and nothing else changes.
    smoke::Field3D field(g);
    fillInterior(field, 1.0f);
    field.at(3, 4, 4) = 6.0f;  // the only non-solid x-neighbour of cell (4,4,4)
    field.at(4, 4, 4) = -100.0f;
    field.at(5, 4, 4) = -100.0f;
    extendIntoSolids(field, lists);
    CHECK(field.at(4, 4, 4) == doctest::Approx((6.0f + 4.0f) / 5.0f));
    CHECK(field.at(5, 4, 4) == doctest::Approx(1.0f));
    CHECK(field.at(3, 4, 4) == 6.0f);

    // A cell on the first interior layer is next to a wall.
    grid.set(1, 4, 4, CellType::Solid);
    CHECK(smoke::buildObstacleLists(grid, layout).touchesWall);
}

TEST_CASE("solid_cells_keep_zero_velocity_and_no_smoke_through_whole_steps") {
    // The phases in step() order, as in SmokeSolver::step(), on a state we can inspect.
    const GridSize g{24, 24, 24};
    SmokeState     s;
    s.resize(g);
    ObstacleGrid grid(g);
    smoke::voxelize(makeSphere(0.5f, 0.45f, 0.5f, 0.15f), {}, grid);
    const smoke::ObstacleLists lists = smoke::buildObstacleLists(grid, s.density);
    REQUIRE(lists.solid.size() > 20);

    smoke::SmokeParams p;
    p.source = {0.5f, 0.15f, 0.5f, 0.1f, 10.0f, 10.0f};
    for (int n = 0; n < 60; ++n) {
        addSources(s, p, p.dt, wholeInterior(g));
        clearSolidScalars(s, p.ambientTemp, lists);
        applyForces(s, p, p.dt, wholeInterior(g));
        applyPorousDrag(s, p.dt, lists);
        enforceSolidVelocity(s, lists);
        advectVelocity(s, p.dt, wholeInterior(g));
        enforceSolidVelocity(s, lists);
        project(s, p, wholeInterior(g), nullptr, &lists);
        extendIntoSolids(s.density, lists);
        extendIntoSolids(s.temperature, lists);
        advectScalars(s, p.dt, wholeInterior(g));
        clearSolidScalars(s, p.ambientTemp, lists);
    }

    // Solid cells that do not touch the fluid: no velocity at all. Every solid cell: no smoke.
    std::vector<std::uint8_t> onSurface(s.density.totalFloats(), 0);
    for (const std::uint32_t cell : lists.boundarySolid) onSurface[cell] = 1;
    double deepSpeed = 0.0, solidDensity = 0.0;
    long long deepCells = 0;
    for (const std::uint32_t cell : lists.solid) {
        solidDensity += std::fabs(s.density.data()[cell]);
        if (onSurface[cell]) continue;
        ++deepCells;
        deepSpeed += std::fabs(s.u.data()[cell]) + std::fabs(s.v.data()[cell]) + std::fabs(s.w.data()[cell]);
    }
    CHECK(deepCells > 0);
    CHECK(deepSpeed == 0.0);
    CHECK(solidDensity == 0.0);

    // On the obstacle surface the normal velocity is zero: a surface cell with exactly one fluid neighbour holds
    // the negative of that neighbour's normal component (and a copy of the other two).
    const float* component[3] = {s.u.data(), s.v.data(), s.w.data()};
    long long    checked = 0, broken = 0;
    for (std::size_t b = 0; b < lists.boundarySolid.size(); ++b) {
        if (lists.neighbourStart[b + 1] - lists.neighbourStart[b] != 1) continue;
        const std::uint32_t cell = lists.boundarySolid[b], from = lists.neighbour[lists.neighbourStart[b]];
        const int           axis = lists.neighbourAxis[lists.neighbourStart[b]];
        for (int c = 0; c < 3; ++c) {
            const float expected = (c == axis) ? -component[c][from] : component[c][from];
            broken += component[c][cell] != expected;
        }
        ++checked;
    }
    CHECK(checked > 0);
    CHECK(broken == 0);
    CHECK(totalMass(s.density) > 0.0);
    CHECK(maxInterior(s.v) > 0.0f);  // the smoke does rise
    CHECK(countNonFinite(s.density) + countNonFinite(s.u) + countNonFinite(s.pressure) == 0);
    // Ghost layers are still consistent.
    CHECK(ghostLayerErrors(s.density, false, false, false) == 0);
    CHECK(ghostLayerErrors(s.u, true, false, false) == 0);
    CHECK(ghostLayerErrors(s.pressure, false, false, false) == 0);
}

TEST_CASE("smoke_goes_around_a_solid_sphere_instead_of_through_it") {
    SolverConfig cfg;
    cfg.grid          = {32, 32, 32};
    cfg.params.source = {0.5f, 0.12f, 0.5f, 0.08f, 10.0f, 10.0f};
    SmokeSolver free(cfg), blocked(cfg);

    ObstacleGrid grid(cfg.grid);
    smoke::voxelize(makeSphere(0.5f, 0.5f, 0.5f, 0.17f), {}, grid);
    REQUIRE(blocked.setObstacles(grid));
    CHECK(blocked.hasObstacles());

    for (int n = 0; n < 600; ++n) {
        free.step();
        blocked.step();
    }

    // Density inside the sphere: zero with the obstacle, clearly non-zero without it (so the test means something).
    double insideFree = 0.0, insideBlocked = 0.0, besideBlocked = 0.0;
    const GridSize g = cfg.grid;
    for (int k = 1; k <= g.nz; ++k)
        for (int j = 1; j <= g.ny; ++j)
            for (int i = 1; i <= g.nx; ++i) {
                if (grid.at(i, j, k) == CellType::Solid) {
                    insideFree += densityAt(free, i, j, k);
                    insideBlocked += densityAt(blocked, i, j, k);
                } else if (j >= 14 && j <= 19) {
                    besideBlocked += densityAt(blocked, i, j, k);  // the height of the sphere, outside it
                }
            }
    const double aboveFree    = massInRows(free, 24, 32);
    const double aboveBlocked = massInRows(blocked, 24, 32);
    MESSAGE("32^3, 600 steps: density inside the sphere region, free / blocked = " << insideFree << " / " << insideBlocked
            << "; beside the sphere (blocked) = " << besideBlocked << "; above it, free / blocked = " << aboveFree << " / "
            << aboveBlocked);
    // Smoke must not vanish at the obstacle: compare the total mass in the box (the source adds the same amount
    // in both runs; the obstacle only removes what the source injects inside it, and it is not near the source).
    const double massFree = free.diagnostics().totalMass, massBlocked = blocked.diagnostics().totalMass;
    MESSAGE("total smoke mass, free / blocked = " << massFree << " / " << massBlocked << " (ratio " << massBlocked / massFree << ")");
    CHECK(insideFree > 1.0);
    CHECK(insideBlocked == 0.0);
    // Measured 2026-11-11 (g++ 16.2.0, PC-2): 822.5 beside the sphere after 600 steps, mass ratio 0.75.
    // With plain zero velocity in the solid cells (the first version) only 12.6 got beside the sphere: the
    // cell touching the obstacle was not constrained and the smoke was lost there. The limit of 100 separates
    // the two clearly.
    CHECK(besideBlocked > 100.0);

    // Later the smoke is above the sphere too. Measured: 1226.6 after 1200 steps (without the sphere 1207.1
    // is there already after 600: the obstacle delays the smoke, it does not stop it).
    for (int n = 0; n < 600; ++n) blocked.step();
    const double aboveLater = massInRows(blocked, 24, 32);
    MESSAGE("blocked, 1200 steps: above the sphere = " << aboveLater << ", total = " << blocked.diagnostics().totalMass);
    CHECK(aboveLater > 100.0);

    const smoke::Diagnostics d = blocked.diagnostics();
    MESSAGE("with the sphere: max|div| before / after projection (fluid cells) = " << d.maxDivBefore << " / " << d.maxDivAfter
            << "; without: " << free.diagnostics().maxDivBefore << " / " << free.diagnostics().maxDivAfter);
    CHECK(d.nonFiniteCells == 0);
}

TEST_CASE("a_porous_layer_slows_the_smoke_down") {
    SolverConfig cfg;
    cfg.grid          = {32, 32, 32};
    cfg.params.source = {0.5f, 0.12f, 0.5f, 0.08f, 10.0f, 10.0f};

    // A porous slab across the whole box, rows 10 .. 12 (the source is around row 4).
    ObstacleGrid slab(cfg.grid);
    for (int k = 1; k <= 32; ++k)
        for (int j = 10; j <= 12; ++j)
            for (int i = 1; i <= 32; ++i) slab.set(i, j, k, CellType::Porous);

    auto massAboveAfter = [&](float drag, float filter) {
        SmokeSolver solver(cfg);
        ObstacleGrid grid = slab;
        grid.porousDrag          = drag;
        grid.porousDensityFilter = filter;
        REQUIRE(solver.setObstacles(grid));
        for (int n = 0; n < 300; ++n) solver.step();
        CHECK(solver.diagnostics().nonFiniteCells == 0);
        return massInRows(solver, 13, 32);
    };

    SmokeSolver free(cfg);
    for (int n = 0; n < 300; ++n) free.step();
    const double aboveFree     = massInRows(free, 13, 32);
    const double aboveNoDrag   = massAboveAfter(0.0f, 0.0f);
    const double aboveDrag     = massAboveAfter(20.0f, 0.0f);
    const double aboveFiltered = massAboveAfter(20.0f, 20.0f);
    MESSAGE("32^3, 300 steps, smoke mass above the slab: no slab " << aboveFree << ", slab with sigma = 0 " << aboveNoDrag
            << ", sigma = 20 " << aboveDrag << ", sigma = 20 and phi = 20 " << aboveFiltered);
    CHECK(aboveFree > 0.0);
    CHECK(aboveNoDrag == aboveFree);       // a porous cell with zero coefficients is plain fluid
    // Measured 2026-11-11: 1007.5 without resistance, 69.4 with sigma = 20, 0.037 with phi = 20 as well.
    CHECK(aboveDrag < aboveFree);          // resistance: less smoke got through in the same time
    CHECK(aboveDrag > 0.0);                // but it does pass
    CHECK(aboveFiltered < aboveDrag);      // filtering removes smoke on top of that
}

TEST_CASE("obstacle_simulation_is_stable_and_deterministic") {
    SolverConfig cfg;
    cfg.grid = {32, 32, 32};
    ObstacleGrid grid(cfg.grid);
    smoke::voxelize(makeBox(0.35f, 0.4f, 0.35f, 0.65f, 0.55f, 0.65f), {}, grid);
    // A solid cell touching the wall as well, to exercise the ghost-layer refresh.
    grid.set(1, 10, 10, CellType::Solid);

    SmokeSolver a(cfg), b(cfg);
    REQUIRE(a.setObstacles(grid));
    REQUIRE(b.setObstacles(grid));
    for (int n = 0; n < 500; ++n) a.step();
    for (int n = 0; n < 500; ++n) b.step();

    const smoke::Diagnostics d = a.diagnostics();
    CHECK(d.nonFiniteCells == 0);
    CHECK(d.totalMass > 0.0);
    CHECK(densityCellsDifferent(a, b) == 0);
    // Compare this number between the build with AVX2 kernels and the one without (SMOKE_ENABLE_AVX2=OFF):
    // the obstacle code itself has one version only, and the kernels are bit-identical in both builds.
    MESSAGE("obstacles, 32^3, 500 steps: density hash = " << densityHash(a) << ", total mass = " << d.totalMass);
}

TEST_CASE("without_obstacles_the_result_is_bit_for_bit_unchanged") {
    SolverConfig cfg;
    cfg.grid = {24, 24, 24};
    SmokeSolver plain(cfg), emptyGrid(cfg), cleared(cfg);

    // An all-fluid grid is the same as no obstacles.
    REQUIRE(emptyGrid.setObstacles(ObstacleGrid(cfg.grid)));
    CHECK_FALSE(emptyGrid.hasObstacles());

    // Obstacles that are removed again before the first step leave no trace.
    ObstacleGrid grid(cfg.grid);
    smoke::voxelize(makeSphere(0.5f, 0.5f, 0.5f, 0.2f), {}, grid);
    REQUIRE(cleared.setObstacles(grid));
    cleared.clearObstacles();

    // A grid of the wrong size is refused.
    CHECK_FALSE(plain.setObstacles(ObstacleGrid(GridSize{8, 8, 8})));

    for (int n = 0; n < 40; ++n) {
        plain.step();
        emptyGrid.step();
        cleared.step();
    }
    CHECK(densityCellsDifferent(plain, emptyGrid) == 0);
    CHECK(densityCellsDifferent(plain, cleared) == 0);

    // resize() drops the obstacles together with the old grid.
    SmokeSolver resized(cfg);
    REQUIRE(resized.setObstacles(grid));
    resized.resize(GridSize{16, 16, 16});
    CHECK_FALSE(resized.hasObstacles());
}
