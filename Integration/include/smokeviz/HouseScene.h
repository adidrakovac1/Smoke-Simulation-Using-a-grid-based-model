#pragma once

#include "smokeviz/BoxOutline.h"
#include "smokeviz/SceneObject.h"
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

/// The house scene (decision D22): a house the camera can walk through, furniture placed with the mouse,
/// 1 to 8 smoke sources and 0 to 8 fans, all inside ONE solver box that is exactly the inside of the house.
///
///   Setup          loads the generated meshes (tools/make_house_meshes.py), the solver, lights and camera
///   OnUpdate       mouse picking and dragging, keys, one simulation step (D17), obstacles rebuilt when needed
///   OnPostRender   uploads the density and draws the smoke, then the overlays (selection, fan reach, porous)
///   OnGlobalSmokeUI  the panel (HouseSceneUI.cpp), shown from the renderer's Smoke menu (D21)
///
/// The walls of the solver box are the outer walls of the house (they are solid in the solver anyway); the
/// inner walls, the furniture and (optionally) the fans are voxelized into one smoke::ObstacleGrid.
class HouseScene : public Scene {
public:
    HouseScene();
    ~HouseScene() override;

    bool Setup();

    void OnUpdate(float deltaTime, IInputProvider& input) override;
    void OnPostRender() override;
    void OnImGuiRender() override {}
    void OnGlobalSmokeUI(bool visible) override;

    static constexpr int kMaxSources = 8;  // = smoke::kMaxSources
    static constexpr int kMaxFans    = 8;  // = smoke::kMaxFans

private:
    // ── Setup ──
    bool LoadHouse();
    void SetupLights();
    void SetupCamera(bool fromAbove);
    void PlaceDefaultLayout();
    void ReadDemoSettings();

    // ── Objects ──
    SceneObject* AddSceneObject(SceneObjectKind kind, const std::string& path, const std::string& name,
                                const glm::vec3& position);
    void         RemoveSceneObject(int index);
    void         SyncRenderItem(SceneObject& object);
    void         SetHouseItemVisible(long long item, bool visible);
    int          CountKind(SceneObjectKind kind) const;
    const MaterialInstance* MaterialFor(const SceneObject& object) const;

    // ── Simulation ──
    void ApplyResolution(int choice);
    void RebuildObstacles();
    void SyncSolverParams();
    [[nodiscard]] float CellSize() const;

    // ── Mouse and keys ──
    void HandleMouse(IInputProvider& input);
    void ClampToHouse(SceneObject& object) const;

    // ── Drawing ──
    void BuildFanRegionMesh();
    void DrawOverlays();

    // ── Panel (HouseSceneUI.cpp) ──
    void DrawPanel();
    void DrawFurnitureTab();
    void DrawSourcesTab();
    void DrawFansTab();
    void DrawSelectedObject();

    // Solver and smoke drawing
    std::unique_ptr<smoke::SmokeSolver> m_solver;
    int                                 m_resolution = 1;  // index into kCellSizes (HouseScene.cpp): 0.075 m
    Texture3D                           m_densityTexture;
    VolumeRaymarchPass                  m_volumePass;
    BoxOutline                          m_outline;
    VolumeSettings                      m_view;
    SmokeUiState                        m_state;
    bool                                m_densityDirty = true;
    std::string                         m_obstacleMessage;
    float                               m_porousDrag   = 20.0f;  // same default as ObstacleSettings
    float                               m_porousFilter = 0.0f;

    // The house (fixed) and the placed objects
    SceneObject m_shell, m_ceiling, m_innerWalls, m_floor, m_ground, m_frames, m_glass;
    bool        m_showCeiling = true;
    std::vector<std::unique_ptr<SceneObject>> m_objects;  // furniture, sources and fans
    std::vector<std::size_t>                  m_freeItems; // RenderItem slots of removed objects, reused
    bool                                      m_obstaclesDirty = true;

    // Renderer resources
    std::shared_ptr<ShaderProgram>    m_meshShader;
    std::shared_ptr<Material>         m_baseMaterial;
    std::unique_ptr<MaterialInstance> m_outerWallMaterial, m_innerWallMaterial, m_floorMaterial, m_ceilingMaterial,
        m_groundMaterial, m_frameMaterial, m_furnitureMaterial, m_sourceMaterial, m_fanMaterial;
    // One colour per generated furniture model, by file name (e.g. "sofa.obj"); others use m_furnitureMaterial.
    std::vector<std::pair<std::string, std::unique_ptr<MaterialInstance>>> m_modelMaterials;
    std::unique_ptr<ShaderProgram> m_overlayShader;  // overlay_mesh.vert + obstacle_mesh.frag
    std::shared_ptr<Mesh>          m_fanRegionMesh;  // unit cylinder along +x, for the fan reach

    // Mouse picking
    int       m_selected = -1;
    bool      m_leftWasDown = false;  // left button in the previous frame (GLFW), for the press edge
    bool      m_dragging = false;
    bool      m_dragMoved = false;
    glm::vec2 m_dragOffset{0.0f};  // object position - grab point, in x and z
    float     m_dragHeight = 0.0f;

    // Panel state
    int       m_modelChoice = 0;
    int       m_pendingResolution = 1;  // chosen in the combo, applied by the button
    char      m_objPath[512] = "";
    glm::vec3 m_newPosition{-3.0f, 0.0f, -2.0f};
    bool      m_showFanReach = true;
    std::string m_message;

    // Light used by the smoke shader and the overlays
    glm::vec3 m_lightDirection{-0.35f, -1.0f, -0.25f};
    glm::vec3 m_lightColor{1.0f, 0.96f, 0.90f};
    glm::vec3 m_cameraAnchor{0.0f};

    int         m_screenshotCountdown = 0;
    int         m_screenshotIndex     = 0;
    std::string m_screenshotPath;
};

} // namespace smokeviz
