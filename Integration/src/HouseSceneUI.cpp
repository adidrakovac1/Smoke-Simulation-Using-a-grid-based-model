// The house scene's ImGui panel (D22). Labels are Bosnian without diacritics: the default ImGui font has no
// glyphs for them (decision D7, the same choice as SmokeUI).
#include "smokeviz/HouseScene.h"

#include "smoke/SmokeSolver.h"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <string>

namespace smokeviz {

namespace {

constexpr const char* kResolutionLabels[] = {"Gruba (0.10 m)", "Srednja (0.075 m)", "Fina (0.06 m)"};

struct Model {
    const char* label;
    const char* file;
};
constexpr Model kModels[] = {{"Stol", "table.obj"},        {"Stolica", "chair.obj"},  {"Kauc", "sofa.obj"},
                             {"Krevet", "bed.obj"},        {"Ormar", "wardrobe.obj"}, {"Polica", "shelf.obj"},
                             {"Kuhinjski element", "kitchen_counter.obj"}};
constexpr int kModelCount = static_cast<int>(sizeof(kModels) / sizeof(kModels[0]));

constexpr const char* kObstacleModes[] = {"Puni", "Ljuska", "Porozni"};

std::string HouseAsset(const char* file) { return std::string(SMOKE_ASSET_DIR) + "/house/" + file; }

} // namespace

void HouseScene::OnGlobalSmokeUI(bool visible) {
    if (visible) DrawPanel();
}

void HouseScene::DrawPanel() {
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(display.x - 15.0f, 60.0f), ImGuiCond_FirstUseEver, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(400.0f, 640.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Kuca (dim)");

    // ── Run control ──
    ImGui::Checkbox("Dim ukljucen", &m_state.enabled);
    ImGui::SameLine();
    if (ImGui::Button(m_state.paused ? "Nastavi" : "Pauza")) m_state.paused = !m_state.paused;
    ImGui::SameLine();
    if (ImGui::Button("Jedan korak")) {
        m_solver->step();
        m_densityDirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset")) {
        m_solver->reset();
        m_densityDirty = true;
    }
    const smoke::GridSize g = m_solver->gridSize();
    ImGui::Text("Korak %lld   mreza %d x %d x %d   %.1f FPS", m_solver->stepCount(), g.nx, g.ny, g.nz,
                ImGui::GetIO().Framerate);

    // A button, not a live combo: resize() reallocates every field and restarts the simulation.
    static_assert(sizeof(kResolutionLabels) / sizeof(kResolutionLabels[0]) == 3);
    ImGui::SetNextItemWidth(170.0f);
    ImGui::Combo("Celija", &m_pendingResolution, kResolutionLabels, 3);
    ImGui::SameLine();
    if (ImGui::Button("Primijeni")) ApplyResolution(m_pendingResolution);

    if (ImGui::Checkbox("Plafon", &m_showCeiling)) SetHouseItemVisible(m_ceiling.item, m_showCeiling);
    ImGui::SameLine();
    if (ImGui::Button("Pogled iz sobe")) {
        SetupCamera(false);
        m_showCeiling = true;
        SetHouseItemVisible(m_ceiling.item, true);
    }
    ImGui::SameLine();
    if (ImGui::Button("Pogled odozgo")) {
        SetupCamera(true);
        m_showCeiling = false;
        SetHouseItemVisible(m_ceiling.item, false);
    }
    ImGui::TextDisabled("Lijevi klik: odaberi i vuci. R: okreni 90. Delete: ukloni.");
    ImGui::TextDisabled("Desni klik + WASD: kamera (Space/Ctrl gore/dolje).");
    if (!m_message.empty()) ImGui::TextWrapped("%s", m_message.c_str());

    if (ImGui::BeginTabBar("house-tabs")) {
        if (ImGui::BeginTabItem("Namjestaj")) {
            DrawFurnitureTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Izvori")) {
            DrawSourcesTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Ventilatori")) {
            DrawFansTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Simulacija")) {
            DrawSimulationParams(m_solver->params());
            ImGui::SeparatorText("Prepreke");
            ImGui::TextWrapped("%s", m_obstacleMessage.c_str());
            DrawStepTimings(*m_solver, m_state.uploadMs);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Prikaz")) {
            // Quick looks; every value stays editable below.
            if (ImGui::Button("Svijetli dim")) {
                m_view.smokeColor = {0.80f, 0.80f, 0.82f};
                m_view.ambient    = 0.30f;
            }
            ImGui::SameLine();
            if (ImGui::Button("Tamni dim")) {
                m_view.smokeColor = {0.28f, 0.28f, 0.30f};
                m_view.ambient    = 0.15f;
            }
            ImGui::SameLine();
            if (ImGui::Button("Bijela para")) {
                m_view.smokeColor = {0.97f, 0.97f, 1.0f};
                m_view.ambient    = 0.45f;
            }
            DrawViewSettings(m_view, &m_state.showOutline);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    DrawSelectedObject();
    ImGui::End();
}

void HouseScene::DrawFurnitureTab() {
    ImGui::SetNextItemWidth(180.0f);
    if (ImGui::BeginCombo("Model", kModels[m_modelChoice].label)) {
        for (int i = 0; i < kModelCount; ++i)
            if (ImGui::Selectable(kModels[i].label, i == m_modelChoice)) m_modelChoice = i;
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Dodaj"))
        AddSceneObject(SceneObjectKind::Furniture, HouseAsset(kModels[m_modelChoice].file), kModels[m_modelChoice].label,
                       m_newPosition);
    ImGui::DragFloat3("Mjesto novog (m)", &m_newPosition.x, 0.05f, -6.0f, 6.0f, "%.2f");
    ImGui::InputText("Putanja .obj", m_objPath, sizeof(m_objPath));
    ImGui::SameLine();
    if (ImGui::Button("Ucitaj")) {
        std::string path = m_objPath;
        const std::size_t slash = path.find_last_of("/\\");
        AddSceneObject(SceneObjectKind::Furniture, path, slash == std::string::npos ? path : path.substr(slash + 1),
                       m_newPosition);
    }
    ImGui::TextDisabled("Vlastiti .obj se ucitava u metrima, kako je u fajlu.");

    ImGui::SeparatorText("Postavljeno");
    for (int i = 0; i < static_cast<int>(m_objects.size()); ++i) {
        const SceneObject& object = *m_objects[static_cast<std::size_t>(i)];
        if (object.Kind() != SceneObjectKind::Furniture) continue;
        char label[96];
        std::snprintf(label, sizeof(label), "%s##furniture-%d", object.Name().c_str(), i);
        if (ImGui::Selectable(label, m_selected == i)) m_selected = i;
    }
    ImGui::SeparatorText("Porozni namjestaj");
    // The porous values go to the solver with the obstacle grid, so they are applied when the slider is released.
    ImGui::SliderFloat("Otpor sigma (1/s)", &m_porousDrag, 0.0f, 100.0f, "%.1f");
    if (ImGui::IsItemDeactivatedAfterEdit()) m_obstaclesDirty = true;
    ImGui::SliderFloat("Filter dima fi (1/s)", &m_porousFilter, 0.0f, 50.0f, "%.1f");
    if (ImGui::IsItemDeactivatedAfterEdit()) m_obstaclesDirty = true;
}

void HouseScene::DrawSourcesTab() {
    const int count = CountKind(SceneObjectKind::Source);
    ImGui::Text("Izvora: %d / %d", count, kMaxSources);
    ImGui::SameLine();
    ImGui::BeginDisabled(count >= kMaxSources);
    if (ImGui::Button("Dodaj izvor"))
        AddSceneObject(SceneObjectKind::Source, HouseAsset("source_marker.obj"), "Izvor", m_newPosition + glm::vec3(0, 0.25f, 0));
    ImGui::EndDisabled();
    ImGui::TextDisabled("Prvi izvor se ne uklanja, ali se moze iskljuciti.");

    int number = 0, removeIndex = -1;
    for (int i = 0; i < static_cast<int>(m_objects.size()); ++i) {
        SceneObject& object = *m_objects[static_cast<std::size_t>(i)];
        if (object.Kind() != SceneObjectKind::Source) continue;
        ++number;
        ImGui::PushID(i);
        char label[48];
        std::snprintf(label, sizeof(label), "Izvor %d", number);
        if (ImGui::Selectable(label, m_selected == i, 0, ImVec2(80.0f, 0.0f))) m_selected = i;
        ImGui::SameLine();
        ImGui::Checkbox("ukljucen", &object.source.enabled);
        if (number > 1) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Ukloni")) removeIndex = i;
        }
        if (ImGui::DragFloat3("Pozicija (m)", &object.position.x, 0.02f, -6.0f, 6.0f, "%.2f")) SyncRenderItem(object);
        if (ImGui::SliderFloat("Radijus (m)", &object.source.radius, 0.05f, 1.0f, "%.2f")) SyncRenderItem(object);
        ImGui::SliderFloat("Gustina/s", &object.source.densityRate, 0.0f, 50.0f, "%.1f");
        ImGui::SliderFloat("Temperatura/s", &object.source.temperatureRate, 0.0f, 50.0f, "%.1f");
        ImGui::Separator();
        ImGui::PopID();
    }
    if (removeIndex >= 0) RemoveSceneObject(removeIndex);
}

void HouseScene::DrawFansTab() {
    const int count = CountKind(SceneObjectKind::Fan);
    ImGui::Text("Ventilatora: %d / %d", count, kMaxFans);
    ImGui::SameLine();
    ImGui::BeginDisabled(count >= kMaxFans);
    if (ImGui::Button("Dodaj ventilator"))
        AddSceneObject(SceneObjectKind::Fan, HouseAsset("fan.obj"), "Ventilator", {m_newPosition.x, 0.0f, m_newPosition.z});
    ImGui::EndDisabled();
    ImGui::Checkbox("Prikazi domet", &m_showFanReach);
    ImGui::TextDisabled("Ventilator tjera zrak u cilindru ispred sebe na zadanu brzinu duz ose (slabi s udaljenoscu).");

    int number = 0, removeIndex = -1;
    for (int i = 0; i < static_cast<int>(m_objects.size()); ++i) {
        SceneObject& object = *m_objects[static_cast<std::size_t>(i)];
        if (object.Kind() != SceneObjectKind::Fan) continue;
        ++number;
        ImGui::PushID(i);
        char label[48];
        std::snprintf(label, sizeof(label), "Ventilator %d", number);
        if (ImGui::Selectable(label, m_selected == i, 0, ImVec2(100.0f, 0.0f))) m_selected = i;
        ImGui::SameLine();
        ImGui::Checkbox("ukljucen", &object.fan.enabled);
        ImGui::SameLine();
        if (ImGui::SmallButton("Ukloni")) removeIndex = i;
        bool moved = ImGui::DragFloat3("Pozicija (m)", &object.position.x, 0.02f, -6.0f, 6.0f, "%.2f");
        bool done  = ImGui::IsItemDeactivatedAfterEdit();
        moved |= ImGui::SliderFloat("Smjer lijevo-desno", &object.yawDegrees, -180.0f, 180.0f, "%.0f deg");
        done |= ImGui::IsItemDeactivatedAfterEdit();
        moved |= ImGui::SliderFloat("Nagib gore-dolje", &object.pitchDegrees, -60.0f, 60.0f, "%.0f deg");
        done |= ImGui::IsItemDeactivatedAfterEdit();
        if (moved) {
            ClampToHouse(object);
            SyncRenderItem(object);
        }
        if (done && object.fan.solid) m_obstaclesDirty = true;
        ImGui::SliderFloat("Radijus (m)", &object.fan.radius, 0.05f, 1.5f, "%.2f");
        ImGui::SliderFloat("Domet (m)", &object.fan.reach, 0.2f, 12.0f, "%.1f");
        ImGui::SliderFloat("Brzina zraka (m/s)", &object.fan.speed, 0.0f, 8.0f, "%.1f");
        ImGui::SliderFloat("Odziv (1/s)", &object.fan.response, 0.5f, 20.0f, "%.1f");
        if (ImGui::Checkbox("Kuciste je prepreka", &object.fan.solid)) m_obstaclesDirty = true;
        ImGui::Separator();
        ImGui::PopID();
    }
    if (removeIndex >= 0) RemoveSceneObject(removeIndex);
}

void HouseScene::DrawSelectedObject() {
    if (m_selected < 0 || m_selected >= static_cast<int>(m_objects.size())) return;
    SceneObject& object = *m_objects[static_cast<std::size_t>(m_selected)];
    ImGui::SeparatorText("Odabrano");
    ImGui::Text("%s  (%zu trouglova)", object.Name().c_str(), object.TriangleCount());

    // Rendering follows every change; the obstacles are rebuilt only when an edit is finished.
    bool moved = ImGui::DragFloat3("Pozicija (m)##selected", &object.position.x, 0.02f, -6.0f, 6.0f, "%.2f");
    bool done  = ImGui::IsItemDeactivatedAfterEdit();
    if (object.Kind() != SceneObjectKind::Source) {
        moved |= ImGui::SliderFloat("Rotacija (deg)##selected", &object.yawDegrees, -180.0f, 180.0f, "%.0f");
        done |= ImGui::IsItemDeactivatedAfterEdit();
    }
    if (object.Kind() == SceneObjectKind::Furniture) {
        moved |= ImGui::SliderFloat("Skala##selected", &object.scale, 0.25f, 3.0f, "%.2f");
        done |= ImGui::IsItemDeactivatedAfterEdit();
        if (ImGui::Combo("Tip prepreke##selected", &object.mode, kObstacleModes, 3)) {
            SyncRenderItem(object);
            m_obstaclesDirty = true;
        }
    }
    if (moved) {
        ClampToHouse(object);
        SyncRenderItem(object);
    }
    const bool affectsObstacles = object.Kind() == SceneObjectKind::Furniture ||
                                  (object.Kind() == SceneObjectKind::Fan && object.fan.solid);
    if (done && affectsObstacles) m_obstaclesDirty = true;
    if (ImGui::Button("Ukloni odabrano")) RemoveSceneObject(m_selected);
}

} // namespace smokeviz
