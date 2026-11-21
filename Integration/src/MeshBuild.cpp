#include "smokeviz/MeshBuild.h"

#include "core/Mesh.h"
#include "core/MeshData.h"

namespace smokeviz {

std::shared_ptr<Mesh> BuildFlatMesh(const std::vector<glm::vec3>& positions, const std::vector<std::uint32_t>& indices,
                                    bool doubleSided, const char* name) {
    MeshData data;
    data.name = name;
    const std::size_t triangles = indices.size() / 3;
    data.vertices.reserve(triangles * (doubleSided ? 6 : 3));
    data.indices.reserve(triangles * (doubleSided ? 6 : 3));
    for (std::size_t t = 0; t < triangles; ++t) {
        const std::uint32_t ia = indices[3 * t], ib = indices[3 * t + 1], ic = indices[3 * t + 2];
        if (ia >= positions.size() || ib >= positions.size() || ic >= positions.size()) continue;
        const glm::vec3 a = positions[ia], b = positions[ib], c = positions[ic];
        glm::vec3       normal = glm::cross(b - a, c - a);
        const float     length = glm::length(normal);
        if (!(length > 0.0f)) continue;  // degenerate triangle: nothing to draw
        normal /= length;
        const glm::vec4     tangent(glm::normalize(b - a), 1.0f);
        const std::uint32_t base = static_cast<std::uint32_t>(data.vertices.size());
        data.vertices.push_back({a, normal, {0.0f, 0.0f}, tangent});
        data.vertices.push_back({b, normal, {1.0f, 0.0f}, tangent});
        data.vertices.push_back({c, normal, {0.0f, 1.0f}, tangent});
        if (doubleSided) {
            data.vertices.push_back({a, -normal, {0.0f, 0.0f}, tangent});
            data.vertices.push_back({c, -normal, {0.0f, 1.0f}, tangent});
            data.vertices.push_back({b, -normal, {1.0f, 0.0f}, tangent});
        }
        const std::uint32_t count = doubleSided ? 6u : 3u;
        for (std::uint32_t k = 0; k < count; ++k) data.indices.push_back(base + k);
    }
    SubMesh subMesh;
    subMesh.name        = name;
    subMesh.indexCount  = static_cast<std::uint32_t>(data.indices.size());
    subMesh.hasTangents = true;
    data.submeshes.push_back(subMesh);
    return data.IsValid() ? std::make_shared<Mesh>(data) : nullptr;
}

} // namespace smokeviz
