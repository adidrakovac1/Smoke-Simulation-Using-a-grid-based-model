#pragma once

#include <array>
#include <cstddef>

namespace smoke {

/// The five phases of one simulation step, in execution order (docs/plan/MATH.md, section 2).
enum class Stage : std::size_t {
    Sources = 0,
    Forces,
    AdvectVelocity,
    Project,
    AdvectScalars,
    Count
};

inline constexpr std::size_t kStageCount = static_cast<std::size_t>(Stage::Count);

inline const char* stageName(Stage s) {
    switch (s) {
        case Stage::Sources:        return "Izvor dima";
        case Stage::Forces:         return "Sile (uzgon, vorticity)";
        case Stage::AdvectVelocity: return "Advekcija brzine";
        case Stage::Project:        return "Projekcija (Jacobi)";
        case Stage::AdvectScalars:  return "Advekcija gustoce";
        default:                    return "?";
    }
}

/// Time spent in each stage during the LAST step, in milliseconds.
struct StepTimings {
    std::array<double, kStageCount> ms{};

    double total() const {
        double t = 0.0;
        for (double v : ms) t += v;
        return t;
    }
    double& operator[](Stage s) { return ms[static_cast<std::size_t>(s)]; }
    double  operator[](Stage s) const { return ms[static_cast<std::size_t>(s)]; }
};

} // namespace smoke
