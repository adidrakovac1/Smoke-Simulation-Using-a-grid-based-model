#include "smokeviz/SmokeScene.h"

#include "Screenshot.h"

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

namespace smokeviz {

namespace {

// Longest side of the box in world units. The solver works in cell units (decision D4), so this number only
// decides how big the smoke looks next to the rest of the scene.
constexpr float kBoxLongestSide = 4.0f;
constexpr float kFloorHalfSize  = 14.0f;

const char* ShaderDir() { return SMOKE_SHADER_DIR; }

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
        spdlog::info("[SmokeScene] automatic screenshot after {} steps -> {}", m_autoShotSteps, m_autoShotFile);
    }
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
}

void SmokeScene::OnImGuiRender() {
    const SmokeUiActions actions = m_ui.Draw(*m_solver, m_view, m_state);

    if (actions.applyGrid) {
        // resize() reallocates all fields and restarts the simulation; the texture follows on the next upload.
        m_solver->resize(smoke::GridSize{actions.newGrid, actions.newGrid, actions.newGrid});
        PlaceBox(m_solver->gridSize());
        m_densityDirty = true;
    }
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
