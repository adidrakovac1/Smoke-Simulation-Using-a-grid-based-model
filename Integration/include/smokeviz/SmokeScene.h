#pragma once

#include "smokeviz/BoxOutline.h"
#include "smokeviz/ObstacleObject.h"
#include "smokeviz/SmokeUI.h"
#include "smokeviz/Texture3D.h"
#include "smokeviz/VolumeRaymarchPass.h"

#include "scene/Scene.h"
#include "smoke/GridSize.h"

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <vector>

class Material;
class MaterialInstance;
class Mesh;
class ShaderProgram;

namespace smoke {
class SmokeSolver;
}

namespace smokeviz {

/// The smoke demo scene: one SmokeSolver in an invisible box, drawn by ray marching.
///
/// This is where the Solver and the Renderer meet (docs/plan/ARCHITECTURE.md, sections 1 and 8):
///   OnUpdate       one simulation step per frame, in the main thread (decision D17)
///   OnPostRender   upload the density to a 3D texture, draw the box outline and the smoke into the HDR buffer
///   OnImGuiRender  the parameter panel
/// The solver works in box coordinates [0,1]^3 and knows nothing about the scene; the box position and size in
/// the world are decided here. "Up" is +Y in both the Renderer and the solver (buoyancy).
class SmokeScene : public Scene {
public:
    SmokeScene();
    ~SmokeScene() override;

    /// Creates the solver, the GPU resources and the scene objects. Needs a current OpenGL context
    /// (call after Application::Initialize()). Returns false if a shader failed to load.
    bool Setup();

    void OnUpdate(float deltaTime, IInputProvider& input) override;
    void OnPostRender() override;
    void OnImGuiRender() override;

private:
    void PlaceBox(const smoke::GridSize& grid);
    void BuildFloor();
    void ReadAutoShotSettings();
    void ApplyObstacle();
    void RemoveObstacle();
    void DrawPorousObstacle();

    std::unique_ptr<smoke::SmokeSolver> m_solver;
    Texture3D                           m_densityTexture;
    VolumeRaymarchPass                  m_volumePass;
    BoxOutline                          m_outline;
    SmokeUI                             m_ui;
    VolumeSettings                      m_view;
    SmokeUiState                        m_state;
    bool                                m_densityDirty = true;  // the texture is older than the solver state

    // World-space box that the grid fills (axis-aligned, standing on the floor).
    glm::vec3 m_boxMin{0.0f};
    glm::vec3 m_boxSize{1.0f};
    glm::vec3 m_cameraAnchor{0.0f};

    glm::vec3 m_lightDirection{-0.45f, -1.0f, -0.35f};  // direction the light travels
    glm::vec3 m_lightColor{1.0f, 0.96f, 0.88f};

    // Floor (drawn by the Renderer's normal mesh path).
    std::shared_ptr<ShaderProgram>    m_meshShader;
    std::shared_ptr<Mesh>             m_floorMesh;
    std::shared_ptr<Material>         m_floorBaseMaterial;
    std::unique_ptr<MaterialInstance> m_floorMaterial;

    // Obstacle (docs/plan/OBSTACLES.md): one .obj mesh, voxelized for the solver and drawn with the same
    // transform. A solid one is an ordinary opaque Renderer object (so it writes depth and hides smoke);
    // a porous one is drawn by us, transparent, after the smoke.
    ObstacleObject                    m_obstacle;
    ObstacleSettings                  m_obstacleSettings;
    bool                              m_obstaclePorous = false;
    std::unique_ptr<MaterialInstance> m_obstacleMaterial;
    std::unique_ptr<ShaderProgram>    m_obstacleShader;
    long long                         m_obstacleItem = -1;  // index of its RenderItem, -1 = not added yet

    // Screenshots: F12 or the panel button; also an automatic one for unattended checks
    // (environment variables SMOKE_AUTOSHOT_STEPS and SMOKE_AUTOSHOT_FILE).
    int         m_screenshotCountdown = 0;   // frames to wait so the requested state is on screen
    std::string m_screenshotPath;
    int         m_screenshotIndex = 0;
    long long   m_autoShotSteps   = 0;       // 0 = off
    std::string m_autoShotFile;
    bool        m_autoShotTaken   = false;
    bool        m_closeAfterShot  = false;
};

} // namespace smokeviz
