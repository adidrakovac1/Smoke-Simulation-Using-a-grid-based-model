#pragma once

#include "smoke/Obstacles.h"

#include <glm/glm.hpp>

#include <memory>
#include <string>

class Mesh;

namespace smokeviz {

/// What the user chose for the obstacle (edited by the ImGui panel).
struct ObstacleSettings {
    char      path[512]  = "";
    int       mode       = 0;                    ///< 0 = solid (filled), 1 = shell, 2 = porous
    glm::vec3 position{0.5f, 0.55f, 0.5f};       ///< centre of the object in box coordinates [0,1]^3
    float     size       = 0.35f;                ///< largest side of the object as a fraction of the longest box side
    glm::vec3 rotationDegrees{0.0f};             ///< Euler angles
    float     porousDrag = 20.0f;                ///< sigma (1/s); chosen for a visible effect, not a measured material
    float     porousFilter = 0.0f;               ///< phi (1/s)
};

/// One mesh loaded from an .obj file, placed in the box.
///
/// The same triangles go to both sides, with the same transform (docs/plan/OBSTACLES.md, section 2.2):
///   - to the solver, in BOX coordinates, to be voxelized;
///   - to the Renderer, in WORLD coordinates, as a Mesh to be drawn.
/// So what is seen and what the smoke collides with cannot drift apart.
class ObstacleObject {
public:
    /// Reads the file with the solver's own .obj parser and rescales the mesh to a unit size around its centre.
    bool Load(const std::string& path, std::string* error);

    /// Applies the settings: fills BoxMesh() and creates the Renderer mesh. Needs a current OpenGL context.
    void Place(const ObstacleSettings& settings, const glm::vec3& boxMin, const glm::vec3& boxSize);

    [[nodiscard]] bool                       IsLoaded() const { return m_unit.triangleCount() > 0; }
    [[nodiscard]] const smoke::TriangleMesh& BoxMesh() const { return m_boxMesh; }
    [[nodiscard]] const Mesh*                RenderMesh() const { return m_renderMesh.get(); }
    [[nodiscard]] std::size_t                TriangleCount() const { return m_unit.triangleCount(); }

private:
    smoke::TriangleMesh   m_unit;     // as loaded, centred, largest side = 1
    smoke::TriangleMesh   m_boxMesh;  // placed, in box coordinates
    std::shared_ptr<Mesh> m_renderMesh;
};

} // namespace smokeviz
