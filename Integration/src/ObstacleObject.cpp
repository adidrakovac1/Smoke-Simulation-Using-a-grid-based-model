#include "smokeviz/ObstacleObject.h"

#include "core/Mesh.h"
#include "core/MeshData.h"
#include "smoke/ObjLoader.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cstdint>

namespace smokeviz {

bool ObstacleObject::Load(const std::string& path, std::string* error) {
    smoke::TriangleMesh mesh;
    if (!smoke::loadObj(path, mesh, error)) return false;

    // Files come in any size and position. Move the centre of the bounding box to the origin and scale the
    // largest side to 1; the panel's "size" then means the same thing for every file.
    float lo[3], hi[3];
    smoke::meshBounds(mesh, lo, hi);
    const float largest = std::max({hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]});
    if (!(largest > 0.0f)) {
        if (error) *error = "the mesh has no size";
        return false;
    }
    for (std::size_t v = 0; v < mesh.vertexCount(); ++v)
        for (std::size_t axis = 0; axis < 3; ++axis)
            mesh.positions[3 * v + axis] = (mesh.positions[3 * v + axis] - 0.5f * (lo[axis] + hi[axis])) / largest;

    m_unit = std::move(mesh);
    m_boxMesh = {};
    m_renderMesh.reset();
    return true;
}

void ObstacleObject::Place(const ObstacleSettings& settings, const glm::vec3& boxMin, const glm::vec3& boxSize) {
    if (!IsLoaded()) return;

    // Model matrix in WORLD space: scale, rotate, then move to the chosen point of the box. The size is a
    // fraction of the longest box side, so the object keeps its proportions in a non-cubic box.
    const float     longest = std::max({boxSize.x, boxSize.y, boxSize.z});
    const glm::vec3 centre  = boxMin + settings.position * boxSize;
    glm::mat4       model   = glm::translate(glm::mat4(1.0f), centre);
    model = glm::rotate(model, glm::radians(settings.rotationDegrees.y), glm::vec3(0, 1, 0));
    model = glm::rotate(model, glm::radians(settings.rotationDegrees.x), glm::vec3(1, 0, 0));
    model = glm::rotate(model, glm::radians(settings.rotationDegrees.z), glm::vec3(0, 0, 1));
    model = glm::scale(model, glm::vec3(settings.size * longest));

    // 1. For the solver: the same world positions, expressed in box coordinates.
    m_boxMesh.indices = m_unit.indices;
    m_boxMesh.positions.resize(m_unit.positions.size());
    std::vector<glm::vec3> world(m_unit.vertexCount());
    for (std::size_t v = 0; v < m_unit.vertexCount(); ++v) {
        world[v] = glm::vec3(model * glm::vec4(m_unit.positions[3 * v], m_unit.positions[3 * v + 1], m_unit.positions[3 * v + 2], 1.0f));
        const glm::vec3 inBox = (world[v] - boxMin) / boxSize;
        m_boxMesh.positions[3 * v]     = inBox.x;
        m_boxMesh.positions[3 * v + 1] = inBox.y;
        m_boxMesh.positions[3 * v + 2] = inBox.z;
    }

    // 2. For the Renderer: flat-shaded triangles in world space (each triangle has its own three vertices and
    //    its face normal). Every triangle is added twice, once per winding, so the object is visible whichever
    //    way the file orders its corners; the Renderer culls back faces.
    MeshData data;
    data.name = "SmokeObstacle";
    data.vertices.reserve(m_unit.triangleCount() * 6);
    data.indices.reserve(m_unit.triangleCount() * 6);
    for (std::size_t t = 0; t < m_unit.triangleCount(); ++t) {
        const glm::vec3 a = world[m_unit.indices[3 * t]], b = world[m_unit.indices[3 * t + 1]], c = world[m_unit.indices[3 * t + 2]];
        glm::vec3       normal = glm::cross(b - a, c - a);
        const float     length = glm::length(normal);
        if (!(length > 0.0f)) continue;  // degenerate triangle: nothing to draw
        normal /= length;
        const glm::vec4 tangent(glm::normalize(b - a), 1.0f);
        const uint32_t  base = static_cast<uint32_t>(data.vertices.size());
        data.vertices.push_back({a, normal, {0.0f, 0.0f}, tangent});
        data.vertices.push_back({b, normal, {1.0f, 0.0f}, tangent});
        data.vertices.push_back({c, normal, {0.0f, 1.0f}, tangent});
        data.vertices.push_back({a, -normal, {0.0f, 0.0f}, tangent});
        data.vertices.push_back({c, -normal, {0.0f, 1.0f}, tangent});
        data.vertices.push_back({b, -normal, {1.0f, 0.0f}, tangent});
        for (uint32_t k = 0; k < 6; ++k) data.indices.push_back(base + k);
    }
    SubMesh subMesh;
    subMesh.name        = "Obstacle";
    subMesh.indexCount  = static_cast<uint32_t>(data.indices.size());
    subMesh.hasTangents = true;
    data.submeshes.push_back(subMesh);
    m_renderMesh = data.IsValid() ? std::make_shared<Mesh>(data) : nullptr;
}

} // namespace smokeviz
