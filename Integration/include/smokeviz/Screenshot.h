#pragma once

#include <string>

namespace smokeviz {

/// Saves the last PRESENTED frame of the window (scene and ImGui panels) as a PNG file.
/// Reads the front buffer of the default framebuffer, so call it at the start of a frame.
/// Returns false if the file could not be written.
bool SaveWindowPng(const std::string& path);

} // namespace smokeviz
