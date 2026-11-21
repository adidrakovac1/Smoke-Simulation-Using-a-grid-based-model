#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <vector>

class Mesh;

namespace smokeviz {

/// Builds a Renderer Mesh with flat shading: every triangle gets its own three vertices and its face normal.
/// With doubleSided every triangle is added twice, once per winding, so it is visible whichever way the file
/// orders its corners (the Renderer culls back faces). Degenerate triangles are skipped. Needs a current OpenGL
/// context; returns null when nothing is left to draw. Used by ObstacleObject and SceneObject (D22).
std::shared_ptr<Mesh> BuildFlatMesh(const std::vector<glm::vec3>& positions, const std::vector<std::uint32_t>& indices,
                                    bool doubleSided, const char* name);

} // namespace smokeviz
