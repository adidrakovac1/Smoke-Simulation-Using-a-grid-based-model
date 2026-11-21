#pragma once

#include "core/Transform.h"
#include "smoke/Obstacles.h"

#include <glm/glm.hpp>

#include <memory>
#include <string>

class Mesh;

namespace smokeviz {

/// What a placed object is in the house scene (D22).
enum class SceneObjectKind { Furniture, Source, Fan };

/// A smoke source in metres (HouseScene turns it into smoke::SmokeSource, relative to the box).
struct SourceSettings {
    float radius          = 0.25f;  ///< sphere radius in metres (2.5 cells on the coarsest grid)
    float densityRate     = 25.0f;  ///< denser than smoke::SmokeSource (10): chosen on screenshots of the house (D22)
    float temperatureRate = 10.0f;  ///< same as smoke::SmokeSource
    bool  enabled         = true;
};

/// A fan in metres (HouseScene turns it into smoke::SmokeFan; see SmokeParams.h for the force).
struct FanSettings {
    float radius   = 0.30f;  ///< radius of the pushed cylinder, metres
    float reach    = 3.00f;  ///< length of the cylinder, metres
    float strength = 0.0f;   ///< m/s^2 at the fan's face; set by HouseScene (kDefaultFanStrength)
    bool  enabled  = true;
    bool  solid    = false;  ///< the fan's body is also a solid obstacle
};

/// One object placed in the house: a piece of furniture, a smoke source marker or a fan.
///
/// The mesh is kept in its own size (metres, as in the file; ObstacleObject instead rescales to a unit size).
/// One Renderer Transform (T * R * S) places it, and the same matrix gives both sides:
///   - the Renderer draws the local mesh with that transform (RenderItem::transform), so dragging only changes
///     the transform, not the mesh;
///   - BoxMesh() transforms the triangles into box coordinates for the solver's voxelizer.
/// So what is seen and what the smoke collides with cannot drift apart (docs/plan/OBSTACLES.md, section 2.2).
class SceneObject {
public:
    /// Reads the .obj with the solver's parser and builds the Renderer mesh. Needs a current OpenGL context.
    bool Load(const std::string& path, const std::string& name, SceneObjectKind kind, std::string* error);

    [[nodiscard]] SceneObjectKind    Kind() const { return m_kind; }
    [[nodiscard]] const std::string& Name() const { return m_name; }
    [[nodiscard]] const std::string& Path() const { return m_path; }
    [[nodiscard]] std::size_t        TriangleCount() const { return m_local.triangleCount(); }
    [[nodiscard]] const Mesh*        RenderMesh() const { return m_mesh.get(); }

    /// Placement. Yaw turns around +y, pitch tilts around the object's own z axis (used for fans).
    glm::vec3 position{0.0f};
    float     yawDegrees   = 0.0f;
    float     pitchDegrees = 0.0f;
    float     scale        = 1.0f;
    int       mode         = 0;  ///< furniture: 0 = solid, 1 = shell, 2 = porous (smoke::ObstacleMode)
    SourceSettings source;
    FanSettings    fan;
    long long      item = -1;    ///< index of its RenderItem in the scene, -1 = not added yet

    /// The transform built from position, yaw, pitch and scale (M = T * R * S, the Renderer's order).
    [[nodiscard]] Transform MakeTransform() const;
    [[nodiscard]] glm::mat4 Model() const { return MakeTransform().GetModelMatrix(); }
    /// Axis-aligned world bounds of the placed mesh (for picking and the selection outline).
    void WorldBounds(glm::vec3& lo, glm::vec3& hi) const;
    /// The placed triangles in box coordinates ([0,1]^3 = the solver box), for smoke::voxelize.
    [[nodiscard]] smoke::TriangleMesh BoxMesh(const glm::vec3& boxMin, const glm::vec3& boxSize) const;
    /// A point given in the mesh's own coordinates, in world space.
    [[nodiscard]] glm::vec3 LocalToWorld(const glm::vec3& local) const;
    /// A direction given in the mesh's own coordinates, in world space (unit length).
    [[nodiscard]] glm::vec3 DirectionToWorld(const glm::vec3& local) const;

private:
    SceneObjectKind       m_kind = SceneObjectKind::Furniture;
    std::string           m_name;
    std::string           m_path;
    smoke::TriangleMesh   m_local;  // as in the file, metres
    glm::vec3             m_lo{0.0f}, m_hi{0.0f};
    std::shared_ptr<Mesh> m_mesh;
};

} // namespace smokeviz
