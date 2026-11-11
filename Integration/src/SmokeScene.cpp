#include "smokeviz/SmokeScene.h"

#include "smokeviz/Screenshot.h"

#include "assets/AssetImporter.h"
#include "core/IInputProvider.h"
#include "core/Material.h"
#include "core/Mesh.h"
#include "core/MeshData.h"
#include "core/ShaderProgram.h"
#include "scene/LightBuilder.h"
#include "smoke/SmokeSolver.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>

namespace smokeviz {

namespace {

// Longest side of the box in world units. The solver works in cell units (decision D4), so this number only
// decides how big the smoke looks next to the rest of the scene.
constexpr float kBoxLongestSide = 4.0f;
constexpr float kFloorHalfSize  = 14.0f;

const char* ShaderDir() { return SMOKE_SHADER_DIR; }
const char* AssetDir() { return SMOKE_ASSET_DIR; }

} // namespace

SmokeScene::SmokeScene()  = default;
SmokeScene::~SmokeScene() = default;

bool SmokeScene::Setup() {
    spdlog::info("[SmokeScene] Setting up (shaders from {})", ShaderDir());
    SetSceneName("Smoke: grid simulation");
    SetClearColor({0.035f, 0.040f, 0.050f, 1.0f});
    SetSkyboxVisible(false);

    // One box = one solver instance (docs/plan/ARCHITECTURE.md, section 8). Default grid is 64^3.
    m_solver = std::make_unique<smoke::SmokeSolver>(smoke::SolverConfig{});
    PlaceBox(m_solver->gridSize());

    if (!m_volumePass.Init(ShaderDir())) {
        spdlog::error("[SmokeScene] volume_raymarch shader failed to load");
        return false;
    }
    if (!m_outline.Init(ShaderDir())) {
        spdlog::error("[SmokeScene] box_outline shader failed to load");
        return false;
    }

    BuildFloor();

    // Transparent pass for porous obstacles; a missing shader only means the blanket is not drawn.
    m_obstacleShader = std::make_unique<ShaderProgram>(std::string(ShaderDir()) + "/obstacle_mesh.vert",
                                                       std::string(ShaderDir()) + "/obstacle_mesh.frag");
    if (m_floorBaseMaterial) {
        m_obstacleMaterial = std::make_unique<MaterialInstance>(m_floorBaseMaterial);
        m_obstacleMaterial->SetName("mat_smoke_obstacle");
        m_obstacleMaterial->SetVec3("u_AlbedoColor", {0.62f, 0.30f, 0.16f});
        m_obstacleMaterial->SetFloat("u_MetallicValue", 0.0f);
        m_obstacleMaterial->SetFloat("u_RoughnessValue", 0.6f);
        m_obstacleMaterial->SetVec3("u_EmissiveColor", {0.0f, 0.0f, 0.0f});
        m_obstacleMaterial->SetFloat("u_EmissiveStrength", 1.0f);
    }

    // One directional light, the same one the smoke shader uses for its shading. No shadow map: smoke is not
    // in the Renderer's shadow pass, so a shadow of the (empty) box would only confuse.
    SetAmbientLight({0.045f, 0.052f, 0.060f}, 0.65f);
    auto& lights = GetLights();
    lights.Clear();
    lights.SetDirectionalLight(DirectionalLightBuilder()
                                   .Direction(m_lightDirection)
                                   .Color(m_lightColor)
                                   .Intensity(2.0f)
                                   .CastShadow(false)
                                   .Name("SmokeSun")
                                   .Build());

    // Orbit camera around the middle of the box (the Renderer's ThirdPerson mode): hold the right mouse button
    // to rotate, scroll to zoom, WASD moves the orbit centre.
    m_cameraAnchor      = m_boxMin + 0.5f * m_boxSize;
    m_cameraOrbitRadius = 2.2f * kBoxLongestSide;
    Camera camera(CameraMode::ThirdPerson);
    camera.SetOrbitTarget(m_cameraAnchor);
    camera.SetOrbitRadius(m_cameraOrbitRadius);
    camera.SetOrientation(-60.0f, -12.0f);
    camera.SetFOV(50.0f);
    SetCamera(camera);

    ReadAutoShotSettings();
    return true;
}

void SmokeScene::PlaceBox(const smoke::GridSize& grid) {
    // Cells are cubes, so the box has the same proportions as the grid; it stands on the floor, centred in x and z.
    const float longest = static_cast<float>(std::max({grid.nx, grid.ny, grid.nz}));
    m_boxSize = kBoxLongestSide * glm::vec3(static_cast<float>(grid.nx), static_cast<float>(grid.ny),
                                            static_cast<float>(grid.nz)) / longest;
    m_boxMin  = glm::vec3(-0.5f * m_boxSize.x, 0.0f, -0.5f * m_boxSize.z);
}

void SmokeScene::BuildFloor() {
    m_meshShader = AssetImporter::LoadShader("assets/shaders/mesh.vert", "assets/shaders/mesh.frag");
    if (!m_meshShader || !m_meshShader->IsValid()) {
        // The smoke itself does not need the floor, so this is only a warning.
        spdlog::warn("[SmokeScene] mesh shader not found (working directory must be Renderer/); no floor");
        return;
    }

    MeshData data;
    data.name     = "SmokeFloor";
    const float h = kFloorHalfSize;
    data.vertices = {
        {{-h, 0.0f, -h}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
        {{ h, 0.0f, -h}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
        {{ h, 0.0f,  h}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
        {{-h, 0.0f,  h}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
    };
    // Counter-clockwise when seen from above (+Y): the Renderer culls back faces, and the floor is looked at
    // from above.
    data.indices = {0, 2, 1, 0, 3, 2};
    SubMesh subMesh;
    subMesh.name        = "Floor";
    subMesh.indexCount  = static_cast<uint32_t>(data.indices.size());
    subMesh.hasTangents = true;
    data.submeshes.push_back(subMesh);
    m_floorMesh = std::make_shared<Mesh>(data);

    m_floorBaseMaterial = std::make_shared<Material>(m_meshShader);
    m_floorBaseMaterial->SetVec4("u_TintColor", {1.0f, 1.0f, 1.0f, 1.0f});
    m_floorBaseMaterial->SetFloat("u_AoStrength", 1.0f);
    m_floorMaterial = std::make_unique<MaterialInstance>(m_floorBaseMaterial);
    m_floorMaterial->SetName("mat_smoke_floor");
    m_floorMaterial->SetVec3("u_AlbedoColor", {0.085f, 0.090f, 0.100f});
    m_floorMaterial->SetFloat("u_MetallicValue", 0.0f);
    m_floorMaterial->SetFloat("u_RoughnessValue", 0.9f);
    m_floorMaterial->SetVec3("u_EmissiveColor", {0.0f, 0.0f, 0.0f});
    m_floorMaterial->SetFloat("u_EmissiveStrength", 1.0f);

    RenderItem item;
    item.meshMulti    = m_floorMesh.get();
    item.subMeshIndex = 0;
    item.material     = m_floorMaterial.get();
    item.transform.SetTranslation({0.0f, -0.01f, 0.0f});  // just below the box bottom
    item.flags.castShadow    = false;
    item.flags.receiveShadow = false;
    AddObject(item);
}

void SmokeScene::ReadAutoShotSettings() {
    // Unattended check: SMOKE_AUTOSHOT_STEPS=300 SMOKE_AUTOSHOT_FILE=out.png  ->  after 300 steps save one
    // screenshot and close the window.
    const char* steps = std::getenv("SMOKE_AUTOSHOT_STEPS");
    const char* file  = std::getenv("SMOKE_AUTOSHOT_FILE");
    if (steps != nullptr && file != nullptr) {
        m_autoShotSteps = std::atoll(steps);
        m_autoShotFile  = file;
        if (const char* grid = std::getenv("SMOKE_AUTOSHOT_GRID")) {
            const int n = std::atoi(grid);
            if (n >= 8 && n <= 256) {
                m_solver->resize(smoke::GridSize{n, n, n});
                PlaceBox(m_solver->gridSize());
            }
        }
        // SMOKE_AUTOSHOT_OBSTACLE=sphere | cube | blanket: put one of the test meshes into the box first.
        if (const char* obstacle = std::getenv("SMOKE_AUTOSHOT_OBSTACLE")) {
            const bool blanket = std::strcmp(obstacle, "blanket") == 0;
            std::snprintf(m_obstacleSettings.path, sizeof(m_obstacleSettings.path), "%s/%s.obj", AssetDir(), obstacle);
            m_obstacleSettings.mode     = blanket ? 2 : 0;
            m_obstacleSettings.position = {0.5f, blanket ? 0.45f : 0.5f, 0.5f};
            m_obstacleSettings.size     = blanket ? 0.7f : 0.3f;
            ApplyObstacle();
        }
        spdlog::info("[SmokeScene] automatic screenshot after {} steps -> {}", m_autoShotSteps, m_autoShotFile);
    }
}

void SmokeScene::ApplyObstacle() {
    std::string error;
    if (!m_obstacle.Load(m_obstacleSettings.path, &error)) {
        m_state.obstacleMessage = "Greska: " + error;
        spdlog::warn("[SmokeScene] obstacle: {}", error);
        return;
    }
    // One transform for both sides: world-space triangles for drawing, box-space triangles for the solver.
    m_obstacle.Place(m_obstacleSettings, m_boxMin, m_boxSize);

    smoke::ObstacleGrid    grid(m_solver->gridSize());
    smoke::VoxelizeOptions options;
    options.mode = m_obstacleSettings.mode == 2   ? smoke::ObstacleMode::Porous
                   : m_obstacleSettings.mode == 1 ? smoke::ObstacleMode::Shell
                                                  : smoke::ObstacleMode::Solid;
    // A solid shell must be at least two cells thick, or pressure leaks through it (OBSTACLES.md, section 5).
    if (options.mode == smoke::ObstacleMode::Shell) options.shellHalfThickness = 1.0f;
    const smoke::VoxelizeStats stats = smoke::voxelize(m_obstacle.BoxMesh(), options, grid);
    grid.porousDrag          = m_obstacleSettings.porousDrag;
    grid.porousDensityFilter = m_obstacleSettings.porousFilter;
    m_solver->setObstacles(grid);

    m_obstaclePorous       = options.mode == smoke::ObstacleMode::Porous;
    m_state.obstacleActive = true;
    m_state.obstacleMessage =
        std::to_string(m_obstacle.TriangleCount()) + " trouglova; celija: cvrstih " +
        std::to_string(grid.count(smoke::CellType::Solid)) + ", poroznih " + std::to_string(grid.count(smoke::CellType::Porous)) +
        " (povrsina " + std::to_string(stats.surfaceCells) + ", unutra " + std::to_string(stats.interiorCells) + ")" +
        (options.mode == smoke::ObstacleMode::Solid && !stats.closed ? "; mesh nije zatvoren, ostala je samo ljuska" : "");
    spdlog::info("[SmokeScene] obstacle '{}': {}", m_obstacleSettings.path, m_state.obstacleMessage);

    // The opaque object the Renderer draws (and whose depth hides the smoke). Porous objects are not in the
    // opaque pass: smoke must stay visible through them.
    if (m_obstacle.RenderMesh() && m_obstacleMaterial) {
        if (m_obstacleItem < 0) {
            RenderItem item;
            item.meshMulti        = m_obstacle.RenderMesh();
            item.material         = m_obstacleMaterial.get();
            item.flags.castShadow = false;
            m_obstacleItem        = static_cast<long long>(AddObject(item));
        }
        RenderItem& item   = GetObject(static_cast<size_t>(m_obstacleItem));
        item.meshMulti     = m_obstacle.RenderMesh();
        item.flags.visible = !m_obstaclePorous;
    }
}

void SmokeScene::RemoveObstacle() {
    m_solver->clearObstacles();
    m_state.obstacleActive  = false;
    m_state.obstacleMessage = "Bez prepreke.";
    if (m_obstacleItem >= 0) GetObject(static_cast<size_t>(m_obstacleItem)).flags.visible = false;
}

// The blanket: drawn after the smoke, blended, tested against the scene depth but not writing it.
void SmokeScene::DrawPorousObstacle() {
    if (!m_state.obstacleActive || !m_obstaclePorous || !m_obstacle.RenderMesh()) return;
    if (!m_obstacleShader || !m_obstacleShader->IsValid()) return;

    const GLboolean hadCull = glIsEnabled(GL_CULL_FACE);
    GLint           cullMode = GL_BACK;
    glGetIntegerv(GL_CULL_FACE_MODE, &cullMode);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    // The mesh holds every triangle in both windings; culling back faces leaves the copy that faces the camera.
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    m_obstacleShader->Bind();
    m_obstacleShader->SetUniform("u_ViewProj", GetCamera().GetViewProjection());
    m_obstacleShader->SetUniform("u_ToLight", glm::normalize(-m_lightDirection));
    m_obstacleShader->SetUniform("u_Color", glm::vec3(0.75f, 0.55f, 0.25f));
    m_obstacleShader->SetUniform("u_Alpha", 0.45f);
    m_obstacle.RenderMesh()->DrawAll();
    ShaderProgram::Unbind();

    glCullFace(static_cast<GLenum>(cullMode));
    if (!hadCull) glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_TRUE);
}

void SmokeScene::OnUpdate(float deltaTime, IInputProvider& input) {
    // A screenshot reads the frame that is on screen now, i.e. the one drawn before this update.
    if (m_screenshotCountdown > 0 && --m_screenshotCountdown == 0) {
        if (SaveWindowPng(m_screenshotPath))
            spdlog::info("[SmokeScene] screenshot saved: {}", m_screenshotPath);
        else
            spdlog::error("[SmokeScene] could not save screenshot: {}", m_screenshotPath);
        if (m_closeAfterShot) glfwSetWindowShouldClose(glfwGetCurrentContext(), GLFW_TRUE);
    }

    glm::vec3 moveDirection{0.0f};
    UpdateStandardCameraAndPlayer(deltaTime, input, m_cameraAnchor, moveDirection, 0.0f);

    if (input.IsKeyPressed(GLFW_KEY_F12) && m_screenshotCountdown == 0) {
        m_screenshotPath      = "smoke_screenshot_" + std::to_string(m_screenshotIndex++) + ".png";
        m_screenshotCountdown = 2;
    }

    // One simulation step per frame with the solver's own dt (not the frame time), so the result is the same
    // on a slow and on a fast frame (decision D17; determinism rule in ARCHITECTURE.md, section 5).
    const bool autoShotPending = m_autoShotSteps > 0 && !m_autoShotTaken;
    if (!m_state.paused && !(autoShotPending && m_solver->stepCount() >= m_autoShotSteps)) {
        m_solver->step();
        m_densityDirty = true;
    }
    if (autoShotPending && m_solver->stepCount() >= m_autoShotSteps && m_screenshotCountdown == 0) {
        m_autoShotTaken       = true;
        m_closeAfterShot      = true;
        m_screenshotPath      = m_autoShotFile;
        m_screenshotCountdown = 3;  // let the last step be drawn and presented first
    }
}

void SmokeScene::OnPostRender() {
    // Called by Application after the opaque objects are in the HDR framebuffer and before tone mapping.
    if (m_densityDirty) {
        const auto start = std::chrono::steady_clock::now();
        m_densityTexture.Upload(m_solver->density());
        m_state.uploadMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        m_densityDirty   = false;
    }

    const Camera& camera = GetCamera();
    if (m_state.showOutline) m_outline.Draw(camera, m_boxMin, m_boxSize, {0.55f, 0.62f, 0.70f});
    m_volumePass.Draw(camera, m_densityTexture, m_boxMin, m_boxSize, m_view, -m_lightDirection, m_lightColor);
    DrawPorousObstacle();
}

void SmokeScene::OnImGuiRender() {
    const SmokeUiActions actions = m_ui.Draw(*m_solver, m_view, m_state, m_obstacleSettings);

    if (actions.applyGrid) {
        // resize() reallocates all fields and restarts the simulation; the texture follows on the next upload.
        m_solver->resize(smoke::GridSize{actions.newGrid, actions.newGrid, actions.newGrid});
        PlaceBox(m_solver->gridSize());
        m_densityDirty = true;
        // resize() dropped the obstacles with the old grid: voxelize the same object for the new one.
        if (m_state.obstacleActive) ApplyObstacle();
    }
    if (actions.applyObstacle) {
        ApplyObstacle();
        m_densityDirty = true;
    }
    if (actions.removeObstacle) RemoveObstacle();
    if (actions.reset) {
        m_solver->reset();
        m_densityDirty = true;
    }
    if (actions.stepOnce) {
        m_solver->step();
        m_densityDirty = true;
    }
    if (actions.screenshot && m_screenshotCountdown == 0) {
        m_screenshotPath      = "smoke_screenshot_" + std::to_string(m_screenshotIndex++) + ".png";
        m_screenshotCountdown = 2;
    }
}

} // namespace smokeviz
