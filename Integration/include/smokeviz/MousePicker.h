#pragma once

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace smokeviz {

/// A ray in world space: origin + t * direction, t >= 0 (direction has unit length).
struct Ray {
    glm::vec3 origin{0.0f};
    glm::vec3 direction{0.0f, 0.0f, -1.0f};
};

/// The view ray through a pixel (D22, dragging furniture with the mouse). The pixel is turned into normalized
/// device coordinates, then the inverse view-projection matrix takes the points on the near and the far plane
/// back to world space; the ray goes from the first to the second. `mouse` and `display` are in the same units
/// (ImGui: window pixels), y grows downwards. The 3D view fills the whole window (the Renderer draws its panels
/// on top of it).
inline Ray RayThroughPixel(const glm::mat4& viewProjection, const glm::vec2& mouse, const glm::vec2& display) {
    const glm::vec2 ndc{2.0f * mouse.x / display.x - 1.0f, 1.0f - 2.0f * mouse.y / display.y};
    const glm::mat4 inverse = glm::inverse(viewProjection);
    glm::vec4       nearPoint = inverse * glm::vec4(ndc, -1.0f, 1.0f);
    glm::vec4       farPoint  = inverse * glm::vec4(ndc, 1.0f, 1.0f);
    nearPoint /= nearPoint.w;
    farPoint /= farPoint.w;
    return {glm::vec3(nearPoint), glm::normalize(glm::vec3(farPoint - nearPoint))};
}

/// Ray / axis-aligned box ("slab" method, the same test as volume_raymarch.frag). Returns the distance to the
/// first hit (0 when the origin is inside), or a negative number when the ray misses.
inline float RayBoxDistance(const Ray& ray, const glm::vec3& lo, const glm::vec3& hi) {
    float tNear = 0.0f, tFar = std::numeric_limits<float>::max();
    for (int axis = 0; axis < 3; ++axis) {
        const float d = ray.direction[axis];
        if (std::abs(d) < 1e-8f) {
            if (ray.origin[axis] < lo[axis] || ray.origin[axis] > hi[axis]) return -1.0f;
            continue;
        }
        float t0 = (lo[axis] - ray.origin[axis]) / d;
        float t1 = (hi[axis] - ray.origin[axis]) / d;
        if (t0 > t1) std::swap(t0, t1);
        tNear = std::max(tNear, t0);
        tFar  = std::min(tFar, t1);
        if (tNear > tFar) return -1.0f;
    }
    return tNear;
}

/// Ray / horizontal plane y = height. Returns false when the ray is parallel to it or the plane is behind.
inline bool RayHorizontalPlane(const Ray& ray, float height, glm::vec3& hit) {
    if (std::abs(ray.direction.y) < 1e-6f) return false;
    const float t = (height - ray.origin.y) / ray.direction.y;
    if (t < 0.0f) return false;
    hit = ray.origin + t * ray.direction;
    return true;
}

} // namespace smokeviz
