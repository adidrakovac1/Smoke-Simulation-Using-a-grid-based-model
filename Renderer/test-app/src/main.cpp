#include "app/Application.h"
#include <spdlog/spdlog.h>
#include <cstddef>
#include <algorithm>
#include <cstdlib>
#include <vector>

#include "BistroScene.h"
#include "CapturePresetScene.h"
#include "TerrainScene.h"
#include "smokeviz/Screenshot.h"
#include "smokeviz/HouseScene.h"
#include "smokeviz/SmokeScene.h"
#include "smokeviz/SmokeSystem.h"

int main()
{
    spdlog::info("[TestApp] Starting");

    Application app;
    if (!app.Initialize())
    {
        spdlog::error("[TestApp] Application::Initialize() failed — aborting");
        return -1;
    }

    TerrainScene terrainScene;
    BistroScene bistroScene;
    CapturePresetScene captureBaseline(CapturePresetKind::Baseline);
    CapturePresetScene captureDenseGrid(CapturePresetKind::DenseGrid);
    CapturePresetScene captureMaterialSweep(CapturePresetKind::MaterialSweep);
    CapturePresetScene captureCulling(CapturePresetKind::CullingTest);
    smokeviz::SmokeScene smokeScene;
    smokeviz::HouseScene houseScene;  // D22

    bool smokeOk = smokeScene.Setup();
    bool houseOk = houseScene.Setup();
    bool terrainOk = terrainScene.Setup();
    bool bistroOk = bistroScene.Setup();
    bool captureBaselineOk = captureBaseline.Setup();
    bool captureDenseGridOk = captureDenseGrid.Setup();
    bool captureMaterialSweepOk = captureMaterialSweep.Setup();
    bool captureCullingOk = captureCulling.Setup();

    if (!smokeOk)
        spdlog::error("[TestApp] SmokeScene::Setup() failed");
    if (!houseOk)
        spdlog::error("[TestApp] HouseScene::Setup() failed");
    if (!terrainOk)
        spdlog::error("[TestApp] TerrainScene::Setup() failed");
    if (!bistroOk)
        spdlog::error("[TestApp] BistroScene::Setup() failed");
    if (!captureBaselineOk)
        spdlog::error("[TestApp] Capture baseline setup failed");
    if (!captureDenseGridOk)
        spdlog::error("[TestApp] Capture dense grid setup failed");
    if (!captureMaterialSweepOk)
        spdlog::error("[TestApp] Capture material sweep setup failed");
    if (!captureCullingOk)
        spdlog::error("[TestApp] Capture culling setup failed");

    std::vector<Scene*> scenes;
    if (terrainOk) scenes.push_back(&terrainScene);
    if (bistroOk) scenes.push_back(&bistroScene);
    if (captureBaselineOk) scenes.push_back(&captureBaseline);
    if (captureDenseGridOk) scenes.push_back(&captureDenseGrid);
    if (captureMaterialSweepOk) scenes.push_back(&captureMaterialSweep);
    if (captureCullingOk) scenes.push_back(&captureCulling);

    // Keep the standalone smoke scene available after the existing scenes. Terrain is the startup view and also
    // demonstrates SmokeVolume as a component inside an ordinary renderer scene.
    std::size_t initialScene = 0;
    if (smokeOk)
    {
        scenes.push_back(&smokeScene);
    }
    // D22: the house scene comes last, so every earlier scene keeps its number key.
    if (houseOk) scenes.push_back(&houseScene);

    // Attach the renderer-owned smoke menu to every ordinary scene. Terrain and the dedicated smoke
    // scene already host their own multi-volume collections, so they keep those scene-aware defaults.
    std::vector<std::unique_ptr<smokeviz::SmokeSystem>> sceneSmokeSystems;
    for (Scene* scene : scenes)
    {
        if (scene == &terrainScene || scene == &smokeScene || scene == &houseScene) continue;
        auto system = std::make_unique<smokeviz::SmokeSystem>();
        if (!system->Initialize()) {
            spdlog::warn("[TestApp] Smoke extension unavailable for {}", scene->GetName());
            continue;
        }
        smokeviz::SmokeSystem* systemPtr = system.get();
        Scene* scenePtr = scene;
        scene->SetSmokeCallbacks(
            [systemPtr]() { systemPtr->Update(); },
            [systemPtr, scenePtr]() { systemPtr->Render(scenePtr->GetCameraForExtensions()); },
            [systemPtr](bool visible) { systemPtr->DrawUI(visible); });
        sceneSmokeSystems.push_back(std::move(system));
    }

    for (std::size_t i = 0; i < scenes.size(); ++i)
        spdlog::info("[TestApp] Press {} for {}", i + 1, scenes[i]->GetName());

    // Unattended check of any scene, used to compare the existing scenes before and after a Renderer change
    // (docs/plan/ARCHITECTURE.md, section 6):
    //   SMOKE_SCENE_SHOT_INDEX=1 SMOKE_SCENE_SHOT_FILE=out.png  ->  draw that scene for 90 frames (or
    //   SMOKE_SCENE_SHOT_FRAMES, D22: the smoke in the house needs longer to spread), save a
    //   picture of the window, exit.
    const char* shotIndex = std::getenv("SMOKE_SCENE_SHOT_INDEX");
    const char* shotFile  = std::getenv("SMOKE_SCENE_SHOT_FILE");
    if (shotIndex != nullptr && shotFile != nullptr && !scenes.empty())
    {
        const std::size_t index = static_cast<std::size_t>(std::atoi(shotIndex)) % scenes.size();
        spdlog::info("[TestApp] Scene shot: {} -> {}", scenes[index]->GetName(), shotFile);
        const char* framesText = std::getenv("SMOKE_SCENE_SHOT_FRAMES");
        const int   frames     = framesText != nullptr ? std::max(1, std::atoi(framesText)) : 90;
        for (int frame = 0; frame < frames; ++frame)
            app.Update(*scenes[index]);
        if (!smokeviz::SaveWindowPng(shotFile))
            spdlog::error("[TestApp] Could not save {}", shotFile);
        return 0;
    }

    if (!scenes.empty())
        app.Run(scenes, initialScene);
    else
        spdlog::error("[TestApp] No scenes were available to run");

    spdlog::info("[TestApp] Shutting down");
    return 0;
}
