#include "smokeviz/SceneObject.h"

#include "smokeviz/MeshBuild.h"

#include "core/Mesh.h"
#include "smoke/ObjLoader.h"

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <vector>

namespace smokeviz {

bool SceneObject::Load(const std::string& path, const std::string& name, SceneObjectKind kind, std::string* error) {
    smoke::TriangleMesh mesh;
    if (!smoke::loadObj(path, mesh, error)) return false;
    if (mesh.triangleCount() == 0) {
        if (error) *error = "the mesh has no triangles";
        return false;
    }
    float lo[3], hi[3];
    smoke::meshBounds(mesh, lo, hi);

    std::vector<glm::vec3> positions(mesh.vertexCount());
    for (std::size_t v = 0; v < mesh.vertexCount(); ++v)
        positions[v] = {mesh.positions[3 * v], mesh.positions[3 * v + 1], mesh.positions[3 * v + 2]};
    // Both windings: furniture loaded from any file stays visible whichever way it orders its corners.
    auto renderMesh = BuildFlatMesh(positions, mesh.indices, true, name.c_str());
    if (!renderMesh) {
        if (error) *error = "nothing to draw (only degenerate triangles)";
        return false;
    }

    m_kind  = kind;
    m_name  = name;
    m_path  = path;
    m_local = std::move(mesh);
    m_lo    = {lo[0], lo[1], lo[2]};
    m_hi    = {hi[0], hi[1], hi[2]};
    m_mesh  = std::move(renderMesh);
    return true;
}

Transform SceneObject::MakeTransform() const {
    Transform transform;
    transform.SetTranslation(position);
    const glm::quat yaw   = glm::angleAxis(glm::radians(yawDegrees), glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::quat pitch = glm::angleAxis(glm::radians(pitchDegrees), glm::vec3(0.0f, 0.0f, 1.0f));
    transform.SetRotation(yaw * pitch);
    transform.SetScale(glm::vec3(scale));
    return transform;
}

void SceneObject::WorldBounds(glm::vec3& lo, glm::vec3& hi) const {
    const glm::mat4 model = Model();
    lo = glm::vec3(1e30f);
    hi = glm::vec3(-1e30f);
    for (int c = 0; c < 8; ++c) {
        const glm::vec3 corner{(c & 1) ? m_hi.x : m_lo.x, (c & 2) ? m_hi.y : m_lo.y, (c & 4) ? m_hi.z : m_lo.z};
        const glm::vec3 world = glm::vec3(model * glm::vec4(corner, 1.0f));
        lo = glm::min(lo, world);
        hi = glm::max(hi, world);
    }
}

smoke::TriangleMesh SceneObject::BoxMesh(const glm::vec3& boxMin, const glm::vec3& boxSize) const {
    const glm::mat4     model = Model();
    smoke::TriangleMesh out;
    out.indices = m_local.indices;
    out.positions.resize(m_local.positions.size());
    for (std::size_t v = 0; v < m_local.vertexCount(); ++v) {
        const glm::vec3 world =
            glm::vec3(model * glm::vec4(m_local.positions[3 * v], m_local.positions[3 * v + 1], m_local.positions[3 * v + 2], 1.0f));
        const glm::vec3 inBox = (world - boxMin) / boxSize;
        out.positions[3 * v]     = inBox.x;
        out.positions[3 * v + 1] = inBox.y;
        out.positions[3 * v + 2] = inBox.z;
    }
    return out;
}

glm::vec3 SceneObject::LocalToWorld(const glm::vec3& local) const {
    return glm::vec3(Model() * glm::vec4(local, 1.0f));
}

glm::vec3 SceneObject::DirectionToWorld(const glm::vec3& local) const {
    const glm::quat yaw   = glm::angleAxis(glm::radians(yawDegrees), glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::quat pitch = glm::angleAxis(glm::radians(pitchDegrees), glm::vec3(0.0f, 0.0f, 1.0f));
    return glm::normalize((yaw * pitch) * local);
}

} // namespace smokeviz
