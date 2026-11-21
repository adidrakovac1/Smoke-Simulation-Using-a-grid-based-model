#include "smokeviz/SmokeSystem.h"

#include "core/Camera.h"
#include <imgui.h>

#include <algorithm>
#include <cstdio>

namespace smokeviz {

bool SmokeSystem::Initialize() {
    // A scene gets controls but no smoke until the user adds a volume.
    return true;
}

void SmokeSystem::SetNewVolumeDefaults(const glm::vec3& position, const glm::vec3& size,
                                       const smoke::GridSize& grid) {
    m_newPosition = position;
    m_newSize     = size;
    m_newGrid     = grid;
}

bool SmokeSystem::AddVolume(const glm::vec3& position, const glm::vec3& size) {
    return AddVolume(position, size, m_newGrid);
}

bool SmokeSystem::AddVolume(const glm::vec3& position, const glm::vec3& size, const smoke::GridSize& grid) {
    auto volume = std::make_unique<SmokeVolume>();
    if (!volume->Initialize(position, glm::max(size, glm::vec3(0.01f)), grid)) return false;
    m_volumes.push_back(std::move(volume));
    return true;
}

void SmokeSystem::Update() {
    for (auto& volume : m_volumes) volume->Update();
}

void SmokeSystem::Render(const Camera& camera) {
    for (auto& volume : m_volumes) volume->Render(camera, m_toLight, m_lightColor);
}

void SmokeSystem::DrawUI(bool visible) {
    if (!visible) return;
    ImGui::Begin("Dimovi##manager");
    ImGui::InputFloat3("Pozicija novog (X Y Z)", &m_newPosition.x, "%.2f");
    ImGui::InputFloat3("Dimenzije novog", &m_newSize.x, "%.2f");
    if (ImGui::Button("Dodaj dim")) AddVolume(m_newPosition, m_newSize);
    ImGui::SameLine();
    ImGui::Text("%d %s", static_cast<int>(m_volumes.size()), m_firstLabel > 1 ? "dodatnih" : "aktivnih");
    int removeIndex = -1;
    for (int i = 0; i < static_cast<int>(m_volumes.size()); ++i) {
        const int label = m_firstLabel + i;
        ImGui::PushID(i);
        ImGui::Text("Dim %d", label);
        ImGui::SameLine();
        if (ImGui::SmallButton("Ukloni")) removeIndex = i;
        ImGui::PopID();
        char title[64];
        std::snprintf(title, sizeof(title), "Dim %d##smoke-volume-%d", label, i);
        m_volumes[i]->DrawUI(title);
    }
    if (removeIndex >= 0)
        m_volumes.erase(m_volumes.begin() + removeIndex);
    ImGui::End();
}

} // namespace smokeviz
