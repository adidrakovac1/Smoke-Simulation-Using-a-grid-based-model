#pragma once

#include "smoke/Diagnostics.h"

#include <string>

namespace smoke {
class SmokeSolver;
}

namespace smokeviz {

struct VolumeSettings;
struct ObstacleSettings;

/// What the user asked for in this frame. The scene carries the requests out, so the panel itself never
/// reallocates the grid or touches OpenGL.
struct SmokeUiActions {
    bool stepOnce   = false;  ///< advance exactly one step (used while paused)
    bool reset      = false;  ///< clear the fields, keep the grid size
    bool applyGrid  = false;  ///< reallocate for newGrid^3 and reset
    int  newGrid    = 64;
    bool screenshot = false;
    bool applyObstacle  = false;  ///< load the .obj in ObstacleSettings, voxelize it, hand it to the solver
    bool removeObstacle = false;
};

/// State owned by the scene that the panel shows and edits.
struct SmokeUiState {
    bool   paused      = false;
    bool   showOutline = true;
    double uploadMs    = 0.0;  ///< time of the last density upload to the GPU
    bool        obstacleActive = false;
    std::string obstacleMessage;   ///< result of the last "apply": cell counts or the error text
};

/// The ImGui panel of the smoke scene: grid size, simulation parameters, source, display settings, timings.
/// Labels are Bosnian without diacritics because the default ImGui font has no glyphs for them (same choice
/// as the console program, decision D7).
class SmokeUI {
public:
    SmokeUiActions Draw(smoke::SmokeSolver& solver, VolumeSettings& view, SmokeUiState& state,
                        ObstacleSettings& obstacle);

private:
    int                m_gridChoice = 1;  // index into {32, 64, 96, 128}
    bool               m_gridSynced = false;
    smoke::Diagnostics m_diagnostics;
};

} // namespace smokeviz
