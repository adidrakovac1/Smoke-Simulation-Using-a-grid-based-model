#include "smokeviz/SmokeUI.h"

#include "smokeviz/VolumeRaymarchPass.h"

#include "smoke/SmokeSolver.h"

#include <imgui.h>

#include <cstddef>

namespace smokeviz {

namespace {
constexpr int         kGridSizes[]  = {32, 64, 96, 128};
constexpr const char* kGridLabels[] = {"32^3", "64^3", "96^3", "128^3"};
constexpr int         kGridCount    = 4;
} // namespace

SmokeUiActions SmokeUI::Draw(smoke::SmokeSolver& solver, VolumeSettings& view, SmokeUiState& state) {
    SmokeUiActions actions;
    smoke::SmokeParams& params = solver.params();
    const smoke::GridSize grid = solver.gridSize();

    // Show the size the solver really has the first time the panel is drawn.
    if (!m_gridSynced) {
        for (int i = 0; i < kGridCount; ++i)
            if (kGridSizes[i] == grid.nx) m_gridChoice = i;
        m_gridSynced = true;
    }

    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(display.x - 15.0f, 60.0f), ImGuiCond_FirstUseEver, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(360.0f, 0.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Dim (simulacija)");

    // ── Run control ──────────────────────────────────────────────────────────
    if (ImGui::Button(state.paused ? "Nastavi" : "Pauza")) state.paused = !state.paused;
    ImGui::SameLine();
    if (ImGui::Button("Jedan korak")) actions.stepOnce = true;
    ImGui::SameLine();
    if (ImGui::Button("Reset")) actions.reset = true;
    ImGui::SameLine();
    if (ImGui::Button("Slika (F12)")) actions.screenshot = true;
    ImGui::Text("Korak: %lld   mreza: %d x %d x %d", solver.stepCount(), grid.nx, grid.ny, grid.nz);

    // ── Grid size ────────────────────────────────────────────────────────────
    // A button, not a live slider: resize() reallocates every field and restarts the simulation
    // (docs/plan/ARCHITECTURE.md, section 3.1).
    ImGui::SeparatorText("Mreza");
    ImGui::SetNextItemWidth(120.0f);
    ImGui::Combo("Velicina", &m_gridChoice, kGridLabels, kGridCount);
    ImGui::SameLine();
    if (ImGui::Button("Apply")) {
        actions.applyGrid = true;
        actions.newGrid   = kGridSizes[m_gridChoice];
    }

    // ── Simulation parameters (docs/plan/MATH.md, sections 2 and 3) ──────────
    ImGui::SeparatorText("Simulacija");
    ImGui::SliderFloat("dt (s)", &params.dt, 1.0f / 240.0f, 1.0f / 15.0f, "%.4f");
    ImGui::SliderInt("Jacobi iteracija K", &params.jacobiIterations, 1, 160);
    ImGui::SliderFloat("Vrtlozenje eps", &params.vorticityEps, 0.0f, 15.0f, "%.2f");
    ImGui::SliderFloat("Uzgon alfa (tezina)", &params.buoyancyAlpha, 0.0f, 1.0f, "%.3f");
    ImGui::SliderFloat("Uzgon beta (toplota)", &params.buoyancyBeta, 0.0f, 10.0f, "%.2f");

    ImGui::SeparatorText("Izvor dima");
    ImGui::SliderFloat("x", &params.source.x, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("y (visina)", &params.source.y, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("z", &params.source.z, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Radijus", &params.source.radius, 0.01f, 0.30f, "%.3f");
    ImGui::SliderFloat("Jacina (gustina/s)", &params.source.densityRate, 0.0f, 50.0f, "%.1f");
    ImGui::SliderFloat("Temperatura/s", &params.source.temperatureRate, 0.0f, 50.0f, "%.1f");

    // ── Display only: these do not change the simulation ─────────────────────
    ImGui::SeparatorText("Prikaz");
    ImGui::SliderFloat("Gustina prikaza", &view.absorption, 0.1f, 40.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
    ImGui::SliderInt("Koraka zrake", &view.steps, 16, 256);
    ImGui::SliderInt("Koraka prema svjetlu", &view.lightSteps, 0, 16);
    ImGui::SliderFloat("Jacina sjene", &view.shadowStrength, 0.0f, 4.0f, "%.2f");
    ImGui::SliderFloat("Ambijent", &view.ambient, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Jacina svjetla", &view.lightIntensity, 0.0f, 5.0f, "%.2f");
    ImGui::ColorEdit3("Boja dima", &view.smokeColor.x);
    ImGui::Checkbox("Jitter", &view.jitter);
    ImGui::SameLine();
    ImGui::Checkbox("Okvir kutije", &state.showOutline);

    // ── Timings of the last step (StepTimings: every stage measures itself) ──
    ImGui::SeparatorText("Vrijeme zadnjeg koraka (ms)");
    const smoke::StepTimings& timings = solver.lastTimings();
    for (std::size_t s = 0; s < smoke::kStageCount; ++s)
        ImGui::Text("%-26s %8.3f", smoke::stageName(static_cast<smoke::Stage>(s)), timings.ms[s]);
    ImGui::Text("%-26s %8.3f", "Ukupno korak", timings.total());
    ImGui::Text("%-26s %8.4f", "Jedna Jacobi iteracija", timings.jacobiIterationMs);
    ImGui::Text("%-26s %8.3f", "Prenos gustine na GPU", state.uploadMs);
    ImGui::Text("FPS: %.1f  (%.2f ms po frejmu)", ImGui::GetIO().Framerate,
                ImGui::GetIO().Framerate > 0.0f ? 1000.0f / ImGui::GetIO().Framerate : 0.0f);

    // ── Health numbers, on demand: diagnostics() makes several passes over the grid ──
    ImGui::SeparatorText("Dijagnostika");
    if (ImGui::Button("Izracunaj")) m_diagnostics = solver.diagnostics();
    if (m_diagnostics.computed) {
        ImGui::Text("max|div| prije / poslije: %.4f / %.4f", m_diagnostics.maxDivBefore, m_diagnostics.maxDivAfter);
        ImGui::Text("ukupna masa: %.1f   max brzina: %.3f", m_diagnostics.totalMass, m_diagnostics.maxSpeed);
        ImGui::Text("CFL: %.3f   NaN/Inf celija: %lld", m_diagnostics.cfl, m_diagnostics.nonFiniteCells);
    }

    ImGui::End();
    return actions;
}

} // namespace smokeviz
