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

void SmokeSystem::AddVolume(const glm::vec3& position, const glm::vec3& size) {
    auto volume = std::make_unique<SmokeVolume>();
    if (volume->Initialize(position, glm::max(size, glm::vec3(0.01f)), {32, 32, 32}))
        m_volumes.push_back(std::move(volume));
}

void SmokeSystem::Update() {
    for (auto& volume : m_volumes) volume->Update();
}

void SmokeSystem::Render(const Camera& camera) {
    for (auto& volume : m_volumes)
        volume->Render(camera, {0.5f, 1.0f, 0.4f}, {1.0f, 0.97f, 0.88f});
}

void SmokeSystem::DrawUI(bool visible) {
    if (!visible) return;
    ImGui::Begin("Dimovi##global-manager");
    ImGui::InputFloat3("Pozicija novog (X Y Z)", &m_newPosition.x, "%.2f");
    ImGui::InputFloat3("Dimenzije novog", &m_newSize.x, "%.2f");
    if (ImGui::Button("Dodaj dim")) AddVolume(m_newPosition, m_newSize);
    ImGui::SameLine();
    ImGui::Text("%d aktivnih", static_cast<int>(m_volumes.size()));
    int removeIndex = -1;
    for (int i = 0; i < static_cast<int>(m_volumes.size()); ++i) {
        ImGui::PushID(i);
        ImGui::Text("Dim %d", i + 1);
        ImGui::SameLine();
        if (ImGui::SmallButton("Ukloni")) removeIndex = i;
        ImGui::PopID();
        char title[64];
        std::snprintf(title, sizeof(title), "Dim %d##global-smoke-%d", i + 1, i);
        m_volumes[i]->DrawUI(title);
    }
    if (removeIndex >= 0)
        m_volumes.erase(m_volumes.begin() + removeIndex);
    ImGui::End();
}

} // namespace smokeviz
