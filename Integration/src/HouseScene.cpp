#include "smokeviz/HouseScene.h"

#include "smokeviz/MeshBuild.h"
#include "smokeviz/MousePicker.h"
#include "smokeviz/Screenshot.h"
#include "smokeviz/SolverSetup.h"

#include "assets/AssetImporter.h"
#include "core/IInputProvider.h"
#include "core/Material.h"
#include "core/Mesh.h"
#include "core/ShaderProgram.h"
#include "scene/LightBuilder.h"
#include "smoke/SmokeSolver.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>

namespace smokeviz {

namespace {

// The inside of the house = the solver box, in metres (the same numbers as tools/make_house_meshes.py).
constexpr glm::vec3 kHouseMin{-6.0f, 0.0f, -4.5f};
constexpr glm::vec3 kHouseSize{12.0f, 3.0f, 9.0f};

// Cell sizes offered in the panel (metres). They divide the house exactly, so the cells stay cubes, which the
// solver assumes (h = 1 in cell units, decision D4): 0.10 -> 120 x 30 x 90, 0.075 -> 160 x 40 x 120,
// 0.06 -> 200 x 50 x 150. Default: 0.075 (index 1, HouseScene.h). Rough check on PC-2 (2026-11-21, one session,
// 15 rounds, median; docs/plan/ARCHITECTURE.md section 6, D22): a step on 0.075 is 2.7x and on 0.06 5.6x the
// step on 0.10; with 0.075 the app ran at about 68 FPS, 0.06 does not fit into a 60 FPS frame.
constexpr float       kCellSizes[]   = {0.10f, 0.075f, 0.06f};
constexpr int         kCellSizeCount = 3;

// Centre of the fan's face in fan.obj coordinates (tools/make_house_meshes.py, fan()).
constexpr glm::vec3 kFanFaceOffset{0.10f, 1.10f, 0.0f};
// Radius of source_marker.obj (metres); the marker is scaled to the source radius.
constexpr float kMarkerRadius = 0.10f;
// Fan strength at the face in m/s^2. Chosen visually (2026-11-21, screenshots from above at 0.10 and 0.075 m):
// the smoke visibly bends along the fan's axis without tearing the plume apart; the panel changes it (0..20).
constexpr float kDefaultFanStrength = 2.0f;

const char* ShaderDir() { return SMOKE_SHADER_DIR; }
std::string HouseAsset(const std::string& file) { return std::string(SMOKE_ASSET_DIR) + "/house/" + file; }

smoke::GridSize GridFor(float cell) {
    return {static_cast<int>(std::lround(kHouseSize.x / cell)), static_cast<int>(std::lround(kHouseSize.y / cell)),
            static_cast<int>(std::lround(kHouseSize.z / cell))};
}

} // namespace

HouseScene::HouseScene()  = default;
HouseScene::~HouseScene() = default;

bool HouseScene::Setup() {
    spdlog::info("[HouseScene] Setting up (meshes from {})", HouseAsset(""));
    SetSceneName("Smoke: house");
    SetClearColor({0.55f, 0.65f, 0.75f, 1.0f});
    SetSkyboxVisible(false);

    // One solver box = the inside of the house, D19/D20 settings (smokeviz/SolverSetup.h).
    m_solver = std::make_unique<smoke::SmokeSolver>(VisualizationSolverConfig(GridFor(kCellSizes[m_resolution])));
    const smoke::GridSize g = m_solver->gridSize();
    spdlog::info("[HouseScene] solver: CpuParallel, {} threads, grid {}x{}x{} ({} m cells)", m_solver->threadCount(),
                 g.nx, g.ny, g.nz, kCellSizes[m_resolution]);
    m_state.showOutline = false;
    // Darker smoke than in the empty smoke scene: white smoke is hard to see in front of light walls.
    m_view.smokeColor = {0.32f, 0.32f, 0.35f};

    if (!m_volumePass.Init(ShaderDir()) || !m_outline.Init(ShaderDir())) {
        spdlog::error("[HouseScene] smoke shaders failed to load");
        return false;
    }
    m_overlayShader = std::make_unique<ShaderProgram>(std::string(ShaderDir()) + "/overlay_mesh.vert",
                                                      std::string(ShaderDir()) + "/obstacle_mesh.frag");
    if (!m_overlayShader->IsValid()) spdlog::warn("[HouseScene] overlay shader failed; sources and fan reach not drawn");

    if (!LoadHouse()) return false;
    BuildFanRegionMesh();
    SetupLights();
    SetupCamera(false);
    PlaceDefaultLayout();
    ReadDemoSettings();
    RebuildObstacles();
    return true;
}

// Unattended checks (with SMOKE_SCENE_SHOT_INDEX / _FILE / _FRAMES in main.cpp):
//   SMOKE_HOUSE_VIEW=above   camera above the house, ceiling hidden
//   SMOKE_HOUSE_FAN=1        one fan in the living room, blowing towards the kitchen
//   SMOKE_HOUSE_CELL=0|1|2   cell size (index into kCellSizes)
void HouseScene::ReadDemoSettings() {
    if (const char* cell = std::getenv("SMOKE_HOUSE_CELL")) {
        m_pendingResolution = std::clamp(std::atoi(cell), 0, kCellSizeCount - 1);
        ApplyResolution(m_pendingResolution);
    }
    if (const char* view = std::getenv("SMOKE_HOUSE_VIEW"); view != nullptr && std::string(view) == "above") {
        SetupCamera(true);
        m_showCeiling = false;
        SetHouseItemVisible(m_ceiling.item, false);
    }
    if (std::getenv("SMOKE_HOUSE_FAN") != nullptr)
        AddSceneObject(SceneObjectKind::Fan, HouseAsset("fan.obj"), "Ventilator", {-2.4f, 0.0f, -3.0f});
    m_selected = -1;
}

// ─── Setup ───────────────────────────────────────────────────────────────────

bool HouseScene::LoadHouse() {
    m_meshShader = AssetImporter::LoadShader("assets/shaders/mesh.vert", "assets/shaders/mesh.frag");
    if (!m_meshShader || !m_meshShader->IsValid()) {
        spdlog::error("[HouseScene] mesh shader not found (the working directory must be Renderer/)");
        return false;
    }
    m_baseMaterial = std::make_shared<Material>(m_meshShader);
    m_baseMaterial->SetVec4("u_TintColor", {1.0f, 1.0f, 1.0f, 1.0f});
    m_baseMaterial->SetFloat("u_AoStrength", 1.0f);
    auto makeMaterial = [&](const char* name, glm::vec3 albedo, float roughness, glm::vec3 emissive) {
        auto m = std::make_unique<MaterialInstance>(m_baseMaterial);
        m->SetName(name);
        m->SetVec3("u_AlbedoColor", albedo);
        m->SetFloat("u_MetallicValue", 0.0f);
        m->SetFloat("u_RoughnessValue", roughness);
        m->SetVec3("u_EmissiveColor", emissive);
        m->SetFloat("u_EmissiveStrength", 1.0f);
        return m;
    };
    m_wallMaterial      = makeMaterial("mat_house_wall", {0.78f, 0.76f, 0.72f}, 0.9f, {0.0f, 0.0f, 0.0f});
    m_floorMaterial     = makeMaterial("mat_house_ceiling", {0.86f, 0.86f, 0.84f}, 0.9f, {0.0f, 0.0f, 0.0f});
    m_furnitureMaterial = makeMaterial("mat_house_furniture", {0.45f, 0.30f, 0.18f}, 0.6f, {0.0f, 0.0f, 0.0f});
    m_sourceMaterial    = makeMaterial("mat_house_source", {0.9f, 0.45f, 0.1f}, 0.5f, {0.9f, 0.4f, 0.05f});
    m_fanMaterial       = makeMaterial("mat_house_fan", {0.30f, 0.38f, 0.48f}, 0.4f, {0.0f, 0.0f, 0.0f});

    struct Part {
        SceneObject*      object;
        const char*       file;
        MaterialInstance* material;
    };
    const Part parts[] = {{&m_shell, "house_shell.obj", m_wallMaterial.get()},
                          {&m_ceiling, "house_ceiling.obj", m_floorMaterial.get()},
                          {&m_innerWalls, "house_inner_walls.obj", m_wallMaterial.get()}};
    for (const Part& part : parts) {
        std::string error;
        if (!part.object->Load(HouseAsset(part.file), part.file, SceneObjectKind::Furniture, &error)) {
            spdlog::error("[HouseScene] {}: {} (run tools/make_house_meshes.py)", part.file, error);
            return false;
        }
        RenderItem item;
        item.meshMulti           = part.object->RenderMesh();
        item.material            = part.material;
        item.flags.castShadow    = false;
        item.flags.receiveShadow = false;
        part.object->item        = static_cast<long long>(AddObject(item));
    }
    return true;
}

void HouseScene::SetupLights() {
    // No shadow maps (smoke is not in the Renderer's shadow pass): a sun that lights every surface by its angle,
    // a bright ambient part, and one point light under the ceiling of each room.
    SetAmbientLight({0.55f, 0.57f, 0.60f}, 0.55f);
    auto& lights = GetLights();
    lights.Clear();
    lights.SetDirectionalLight(DirectionalLightBuilder()
                                   .Direction(m_lightDirection)
                                   .Color(m_lightColor)
                                   .Intensity(1.6f)
                                   .CastShadow(false)
                                   .Name("HouseSun")
                                   .Build());
    const glm::vec3 roomCentres[] = {{-2.5f, 2.7f, -2.0f}, {3.5f, 2.7f, -2.0f}, {-3.0f, 2.7f, 2.5f}, {3.0f, 2.7f, 2.5f}};
    for (const glm::vec3& centre : roomCentres) {
        PointLight light;
        light.position  = centre;
        light.color     = {1.0f, 0.92f, 0.80f};
        light.intensity = 2.0f;
        light.radius    = 6.0f;
        light.name      = "HouseRoomLight";
        lights.AddPointLight(light);
    }
}

void HouseScene::SetupCamera(bool fromAbove) {
    // FreeFly: right mouse button looks around, WASD moves, Space / Ctrl up and down (CameraController.cpp).
    Camera camera(CameraMode::FreeFly);
    if (fromAbove) {
        camera.SetPosition({0.0f, 15.0f, 7.5f});
        camera.SetOrientation(-90.0f, -62.0f);
    } else {
        camera.SetPosition({-5.2f, 1.6f, -4.0f});  // eye height in the corner of the living room
        camera.SetOrientation(35.0f, -12.0f);
    }
    camera.SetFOV(70.0f);
    camera.SetClipPlanes(0.05f, 200.0f);
    SetCamera(camera);
    m_cameraFreeFlySpeed = 2.5f;
    m_cameraAnchor       = camera.GetPosition();
}

void HouseScene::PlaceDefaultLayout() {
    struct Piece {
        const char* file;
        const char* name;
        glm::vec3   position;
        float       yaw;
    };
    // Living room (south-west), kitchen (south-east), bedroom (north-west), study (north-east).
    const Piece pieces[] = {
        {"sofa.obj", "Kauc", {-3.5f, 0.0f, -3.9f}, 180.0f},  {"table.obj", "Stol", {-3.0f, 0.0f, -1.6f}, 0.0f},
        {"chair.obj", "Stolica", {-3.0f, 0.0f, -0.9f}, 0.0f}, {"chair.obj", "Stolica", {-3.0f, 0.0f, -2.3f}, 180.0f},
        {"kitchen_counter.obj", "Kuhinjski element", {4.2f, 0.0f, -4.1f}, 0.0f},
        {"bed.obj", "Krevet", {-3.5f, 0.0f, 3.4f}, 0.0f},     {"wardrobe.obj", "Ormar", {-0.5f, 0.0f, 2.2f}, 90.0f},
        {"shelf.obj", "Polica", {3.0f, 0.0f, 4.2f}, 0.0f},
    };
    for (const Piece& piece : pieces)
        if (SceneObject* object = AddSceneObject(SceneObjectKind::Furniture, HouseAsset(piece.file), piece.name, piece.position)) {
            object->yawDegrees = piece.yaw;
            SyncRenderItem(*object);
        }
    // One smoke source on the floor of the living room, between the table and the kitchen.
    if (SceneObject* source = AddSceneObject(SceneObjectKind::Source, HouseAsset("source_marker.obj"), "Izvor",
                                             {-1.2f, 0.25f, -2.6f}))
        SyncRenderItem(*source);
    m_selected = -1;
}

// ─── Objects ─────────────────────────────────────────────────────────────────

SceneObject* HouseScene::AddSceneObject(SceneObjectKind kind, const std::string& path, const std::string& name,
                                        const glm::vec3& position) {
    if (kind == SceneObjectKind::Source && CountKind(kind) >= kMaxSources) return nullptr;
    if (kind == SceneObjectKind::Fan && CountKind(kind) >= kMaxFans) return nullptr;

    auto        object = std::make_unique<SceneObject>();
    std::string error;
    if (!object->Load(path, name, kind, &error)) {
        m_message = "Greska (" + path + "): " + error;
        spdlog::warn("[HouseScene] {}", m_message);
        return nullptr;
    }
    object->position = position;
    if (kind == SceneObjectKind::Fan) object->fan.strength = kDefaultFanStrength;

    // Sources are drawn as transparent overlays (an opaque marker would hide the smoke it makes), so they get
    // no RenderItem. Furniture and fans are ordinary opaque objects of the Renderer.
    if (kind != SceneObjectKind::Source) {
        RenderItem item;
        item.meshMulti           = object->RenderMesh();
        item.material            = MaterialFor(*object);
        item.flags.castShadow    = false;
        item.flags.receiveShadow = false;
        if (!m_freeItems.empty()) {
            object->item = static_cast<long long>(m_freeItems.back());
            m_freeItems.pop_back();
            GetObject(static_cast<std::size_t>(object->item)) = item;
        } else {
            object->item = static_cast<long long>(AddObject(item));
        }
    }
    ClampToHouse(*object);
    SyncRenderItem(*object);
    m_objects.push_back(std::move(object));
    m_selected       = static_cast<int>(m_objects.size()) - 1;
    m_obstaclesDirty = true;
    return m_objects.back().get();
}

void HouseScene::RemoveSceneObject(int index) {
    if (index < 0 || index >= static_cast<int>(m_objects.size())) return;
    SceneObject& object = *m_objects[static_cast<std::size_t>(index)];
    // The first smoke source stays (it can be switched off instead): the scene always has 1 to 8 sources.
    if (object.Kind() == SceneObjectKind::Source && CountKind(SceneObjectKind::Source) <= 1) {
        m_message = "Prvi izvor se ne uklanja; iskljuci ga ako ne treba.";
        return;
    }
    if (object.item >= 0) {
        // The Scene has no "remove": hide the slot and reuse it for the next object. The slot keeps pointing at
        // a mesh that stays alive (the house shell), never at the mesh that is about to be freed.
        RenderItem& item   = GetObject(static_cast<std::size_t>(object.item));
        item.flags.visible = false;
        item.meshMulti     = m_shell.RenderMesh();
        m_freeItems.push_back(static_cast<std::size_t>(object.item));
    }
    m_objects.erase(m_objects.begin() + index);
    if (m_selected == index) m_selected = -1;
    else if (m_selected > index) --m_selected;
    m_dragging       = false;
    m_obstaclesDirty = true;
}

void HouseScene::SyncRenderItem(SceneObject& object) {
    if (object.Kind() == SceneObjectKind::Source) object.scale = std::max(object.source.radius, 0.01f) / kMarkerRadius;
    if (object.item < 0) return;
    RenderItem& item = GetObject(static_cast<std::size_t>(object.item));
    item.transform   = object.MakeTransform();
    item.material    = MaterialFor(object);
    // Porous furniture is drawn as a transparent overlay after the smoke (smoke must stay visible through it).
    item.flags.visible = !(object.Kind() == SceneObjectKind::Furniture && object.mode == 2);
}

void HouseScene::SetHouseItemVisible(long long item, bool visible) {
    if (item >= 0) GetObject(static_cast<std::size_t>(item)).flags.visible = visible;
}

int HouseScene::CountKind(SceneObjectKind kind) const {
    return static_cast<int>(std::count_if(m_objects.begin(), m_objects.end(),
                                          [kind](const auto& o) { return o->Kind() == kind; }));
}

const MaterialInstance* HouseScene::MaterialFor(const SceneObject& object) const {
    switch (object.Kind()) {
        case SceneObjectKind::Fan: return m_fanMaterial.get();
        case SceneObjectKind::Source: return m_sourceMaterial.get();
        default: return m_furnitureMaterial.get();
    }
}

// ─── Simulation ──────────────────────────────────────────────────────────────

float HouseScene::CellSize() const { return kHouseSize.x / static_cast<float>(m_solver->gridSize().nx); }

void HouseScene::ApplyResolution(int choice) {
    m_resolution = std::clamp(choice, 0, kCellSizeCount - 1);
    m_solver->resize(GridFor(kCellSizes[m_resolution]));  // also drops the obstacles: rebuilt below
    m_densityDirty   = true;
    m_obstaclesDirty = true;
    const smoke::GridSize g = m_solver->gridSize();
    spdlog::info("[HouseScene] grid {}x{}x{} ({} m cells)", g.nx, g.ny, g.nz, kCellSizes[m_resolution]);
}

// Inner walls, furniture and solid fans -> one ObstacleGrid (voxelize adds to what the grid already holds,
// Obstacles.h). Called when something that the smoke collides with has changed, never while dragging.
void HouseScene::RebuildObstacles() {
    const auto      start = std::chrono::steady_clock::now();
    smoke::ObstacleGrid grid(m_solver->gridSize());

    smoke::VoxelizeOptions solid;
    smoke::voxelize(m_innerWalls.BoxMesh(kHouseMin, kHouseSize), solid, grid);
    int pieces = 0;
    for (const auto& object : m_objects) {
        smoke::VoxelizeOptions options;
        if (object->Kind() == SceneObjectKind::Furniture) {
            options.mode = object->mode == 2   ? smoke::ObstacleMode::Porous
                           : object->mode == 1 ? smoke::ObstacleMode::Shell
                                               : smoke::ObstacleMode::Solid;
            // A shell must be at least two cells thick, or pressure leaks through it (OBSTACLES.md, section 5).
            if (options.mode == smoke::ObstacleMode::Shell) options.shellHalfThickness = 1.0f;
        } else if (!(object->Kind() == SceneObjectKind::Fan && object->fan.solid)) {
            continue;  // sources never, fans only when their body is an obstacle
        }
        smoke::voxelize(object->BoxMesh(kHouseMin, kHouseSize), options, grid);
        ++pieces;
    }
    grid.porousDrag          = m_porousDrag;
    grid.porousDensityFilter = m_porousFilter;
    m_solver->setObstacles(grid);
    m_obstaclesDirty = false;

    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    m_obstacleMessage = "Prepreke: zidovi + " + std::to_string(pieces) + " objekata; celija cvrstih " +
                        std::to_string(grid.count(smoke::CellType::Solid)) + ", poroznih " +
                        std::to_string(grid.count(smoke::CellType::Porous)) + " (" + std::to_string(static_cast<int>(ms)) +
                        " ms)";
    spdlog::info("[HouseScene] {}", m_obstacleMessage);
}

// Sources and fans live in metres in the scene; the solver wants them relative to the box (SmokeParams.h).
// Positions: (p - boxMin) / boxSize per axis. Radius and reach: a fraction of nx, i.e. metres / boxSize.x,
// because the cells are cubes. Fan strength: m/s^2 -> cells/s^2 by dividing by the cell size h (D4).
void HouseScene::SyncSolverParams() {
    smoke::SmokeParams& p = m_solver->params();
    const float         h = CellSize();
    int sources = 0, fans = 0;
    for (const auto& object : m_objects) {
        if (object->Kind() == SceneObjectKind::Source && sources < kMaxSources) {
            const glm::vec3   rel = (object->position - kHouseMin) / kHouseSize;
            smoke::SmokeSource src;
            src.x = rel.x;
            src.y = rel.y;
            src.z = rel.z;
            src.radius          = object->source.enabled ? object->source.radius / kHouseSize.x : 0.0f;
            src.densityRate     = object->source.densityRate;
            src.temperatureRate = object->source.temperatureRate;
            if (sources == 0) p.source = src;
            else p.extraSources[static_cast<std::size_t>(sources - 1)] = src;
            ++sources;
        } else if (object->Kind() == SceneObjectKind::Fan && fans < kMaxFans) {
            const glm::vec3 rel = (object->LocalToWorld(kFanFaceOffset) - kHouseMin) / kHouseSize;
            const glm::vec3 dir = object->DirectionToWorld({1.0f, 0.0f, 0.0f});
            smoke::SmokeFan fan;
            fan.x = rel.x;
            fan.y = rel.y;
            fan.z = rel.z;
            fan.dirX = dir.x;
            fan.dirY = dir.y;
            fan.dirZ = dir.z;
            fan.radius   = object->fan.radius / kHouseSize.x;
            fan.reach    = object->fan.reach / kHouseSize.x;
            fan.strength = object->fan.enabled ? object->fan.strength / h : 0.0f;
            p.fans[static_cast<std::size_t>(fans)] = fan;
            ++fans;
        }
    }
    if (sources == 0) p.source.radius = 0.0f;
    p.extraSourceCount = std::max(0, sources - 1);
    p.fanCount         = fans;
}

// ─── Mouse and keys ──────────────────────────────────────────────────────────

void HouseScene::ClampToHouse(SceneObject& object) const {
    glm::vec3 lo, hi;
    object.WorldBounds(lo, hi);
    const glm::vec3 houseMax = kHouseMin + kHouseSize;
    for (int axis : {0, 2}) {
        if (hi[axis] - lo[axis] >= kHouseSize[axis]) continue;  // bigger than the house: leave it
        if (lo[axis] < kHouseMin[axis]) object.position[axis] += kHouseMin[axis] - lo[axis];
        else if (hi[axis] > houseMax[axis]) object.position[axis] -= hi[axis] - houseMax[axis];
    }
    if (object.Kind() != SceneObjectKind::Source && lo.y < 0.0f) object.position.y -= lo.y;
}

// Left click picks the nearest object under the mouse (ray / bounding box) and drags it over a horizontal plane
// at its own height; the obstacles are rebuilt when the button is released. R turns the selection by 90 degrees,
// Delete removes it. Clicks on ImGui windows and the right button (camera) are left alone.
void HouseScene::HandleMouse(IInputProvider& input) {
    const ImGuiIO& io      = ImGui::GetIO();
    const bool     overUi  = io.WantCaptureMouse;
    const glm::vec2 mouse{io.MousePos.x, io.MousePos.y};
    const glm::vec2 display{io.DisplaySize.x, io.DisplaySize.y};
    if (display.x <= 0.0f || display.y <= 0.0f) return;

    if (input.IsMouseButtonPressed(GLFW_MOUSE_BUTTON_LEFT) && !overUi) {
        const Ray ray  = RayThroughPixel(GetCamera().GetViewProjection(), mouse, display);
        int       best = -1;
        float     bestDistance = 1e30f;
        for (int i = 0; i < static_cast<int>(m_objects.size()); ++i) {
            glm::vec3 lo, hi;
            m_objects[static_cast<std::size_t>(i)]->WorldBounds(lo, hi);
            const float t = RayBoxDistance(ray, lo, hi);
            if (t >= 0.0f && t < bestDistance) {
                bestDistance = t;
                best         = i;
            }
        }
        m_selected = best;
        if (best >= 0) {
            SceneObject& object = *m_objects[static_cast<std::size_t>(best)];
            m_dragHeight        = object.position.y;
            glm::vec3 hit;
            if (RayHorizontalPlane(ray, m_dragHeight, hit)) {
                m_dragOffset = {object.position.x - hit.x, object.position.z - hit.z};
                m_dragging   = true;
                m_dragMoved  = false;
            }
        }
    }

    if (m_dragging && m_selected >= 0) {
        if (input.IsMouseButtonDown(GLFW_MOUSE_BUTTON_LEFT)) {
            const Ray ray = RayThroughPixel(GetCamera().GetViewProjection(), mouse, display);
            glm::vec3 hit;
            if (RayHorizontalPlane(ray, m_dragHeight, hit)) {
                SceneObject& object = *m_objects[static_cast<std::size_t>(m_selected)];
                const glm::vec3 before = object.position;
                object.position.x = hit.x + m_dragOffset.x;
                object.position.z = hit.z + m_dragOffset.y;
                ClampToHouse(object);
                if (object.position != before) {
                    m_dragMoved = true;
                    SyncRenderItem(object);
                }
            }
        } else {
            m_dragging = false;
            if (m_dragMoved) m_obstaclesDirty = true;
        }
    }

    if (m_selected >= 0 && !input.ShouldBlockKeyboard()) {
        SceneObject& object = *m_objects[static_cast<std::size_t>(m_selected)];
        if (input.IsKeyPressed(GLFW_KEY_R) && object.Kind() != SceneObjectKind::Source) {
            object.yawDegrees = std::fmod(object.yawDegrees + 90.0f, 360.0f);
            ClampToHouse(object);
            SyncRenderItem(object);
            m_obstaclesDirty = true;
        }
        if (input.IsKeyPressed(GLFW_KEY_DELETE)) RemoveSceneObject(m_selected);
        else if (input.IsKeyPressed(GLFW_KEY_ESCAPE)) m_selected = -1;
    }
}

// ─── Frame ───────────────────────────────────────────────────────────────────

void HouseScene::OnUpdate(float deltaTime, IInputProvider& input) {
    if (m_screenshotCountdown > 0 && --m_screenshotCountdown == 0) {
        if (SaveWindowPng(m_screenshotPath)) spdlog::info("[HouseScene] screenshot saved: {}", m_screenshotPath);
    }
    glm::vec3 moveDirection{0.0f};
    UpdateStandardCameraAndPlayer(deltaTime, input, m_cameraAnchor, moveDirection, 0.0f);
    HandleMouse(input);
    if (input.IsKeyPressed(GLFW_KEY_F12) && m_screenshotCountdown == 0) {
        m_screenshotPath      = "house_screenshot_" + std::to_string(m_screenshotIndex++) + ".png";
        m_screenshotCountdown = 2;
    }

    if (m_obstaclesDirty && !m_dragging) RebuildObstacles();
    SyncSolverParams();
    // One step per frame with the solver's own dt (decision D17), as in SmokeScene.
    if (m_state.enabled && !m_state.paused) {
        m_solver->step();
        m_densityDirty = true;
    }
}

void HouseScene::OnPostRender() {
    // Called after the opaque objects (house, furniture, fans) are in the HDR buffer: their depth hides the
    // smoke behind them (D13), and the camera may be inside the box (volume_raymarch.frag starts the ray at it).
    if (m_state.enabled) {
        if (m_densityDirty) {
            const auto start = std::chrono::steady_clock::now();
            m_densityTexture.Upload(m_solver->density());
            m_state.uploadMs =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            m_densityDirty = false;
        }
        m_volumePass.Draw(GetCamera(), m_densityTexture, kHouseMin, kHouseSize, m_view, -m_lightDirection, m_lightColor);
    }
    if (m_state.showOutline) m_outline.Draw(GetCamera(), kHouseMin, kHouseSize, {0.55f, 0.62f, 0.70f});
    DrawOverlays();
}

// ─── Drawing ─────────────────────────────────────────────────────────────────

// A closed cylinder of radius 1 along +x from x = 0 to x = 1; scaled to the fan's reach and radius.
void HouseScene::BuildFanRegionMesh() {
    constexpr int          kSegments = 24;
    std::vector<glm::vec3> positions;
    std::vector<std::uint32_t> indices;
    for (int s = 0; s < kSegments; ++s) {
        const float a = 2.0f * 3.14159265f * static_cast<float>(s) / kSegments;
        positions.push_back({0.0f, std::cos(a), std::sin(a)});
        positions.push_back({1.0f, std::cos(a), std::sin(a)});
    }
    const std::uint32_t c0 = static_cast<std::uint32_t>(positions.size());
    positions.push_back({0.0f, 0.0f, 0.0f});
    positions.push_back({1.0f, 0.0f, 0.0f});
    for (std::uint32_t s = 0; s < kSegments; ++s) {
        const std::uint32_t n = (s + 1) % kSegments;
        indices.insert(indices.end(), {2 * s, 2 * n, 2 * n + 1, 2 * s, 2 * n + 1, 2 * s + 1});
        indices.insert(indices.end(), {c0, 2 * n, 2 * s, c0 + 1, 2 * s + 1, 2 * n + 1});
    }
    m_fanRegionMesh = BuildFlatMesh(positions, indices, true, "FanReach");
}

void HouseScene::DrawOverlays() {
    const Camera& camera = GetCamera();
    if (m_selected >= 0) {
        glm::vec3 lo, hi;
        m_objects[static_cast<std::size_t>(m_selected)]->WorldBounds(lo, hi);
        m_outline.Draw(camera, lo, hi - lo, {1.0f, 0.85f, 0.2f});
    }
    if (!m_overlayShader || !m_overlayShader->IsValid()) return;

    const GLboolean hadCull = glIsEnabled(GL_CULL_FACE);
    GLint           cullMode = GL_BACK;
    glGetIntegerv(GL_CULL_FACE_MODE, &cullMode);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);  // premultiplied, like the smoke
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);  // every mesh holds both windings; this keeps the side facing the camera

    m_overlayShader->Bind();
    m_overlayShader->SetUniform("u_ViewProj", camera.GetViewProjection());
    m_overlayShader->SetUniform("u_ToLight", glm::normalize(-m_lightDirection));
    auto draw = [&](const Mesh* mesh, const glm::mat4& model, const glm::vec3& colour, float alpha) {
        if (!mesh) return;
        m_overlayShader->SetUniform("u_Model", model);
        m_overlayShader->SetUniform("u_Color", colour);
        m_overlayShader->SetUniform("u_Alpha", alpha);
        mesh->DrawAll();
    };
    for (const auto& object : m_objects) {
        switch (object->Kind()) {
            case SceneObjectKind::Source:  // the sphere the smoke is injected into
                draw(object->RenderMesh(), object->Model(),
                     object->source.enabled ? glm::vec3(1.0f, 0.55f, 0.15f) : glm::vec3(0.4f), 0.35f);
                break;
            case SceneObjectKind::Fan:
                if (m_showFanReach && object->fan.enabled && m_fanRegionMesh) {
                    // Rotation without the object's scale: reach and radius are metres of their own.
                    const glm::vec3 face = object->LocalToWorld(kFanFaceOffset);
                    Transform reach;
                    reach.SetTranslation(face);
                    reach.SetRotation(object->MakeTransform().GetRotation());
                    reach.SetScale({object->fan.reach, object->fan.radius, object->fan.radius});
                    draw(m_fanRegionMesh.get(), reach.GetModelMatrix(), {0.35f, 0.65f, 1.0f}, 0.12f);
                }
                break;
            case SceneObjectKind::Furniture:
                if (object->mode == 2) draw(object->RenderMesh(), object->Model(), {0.75f, 0.55f, 0.25f}, 0.45f);
                break;
        }
    }
    ShaderProgram::Unbind();

    glCullFace(static_cast<GLenum>(cullMode));
    if (!hadCull) glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_TRUE);
}

} // namespace smokeviz
