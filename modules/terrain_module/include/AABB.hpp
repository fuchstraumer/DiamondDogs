#pragma once
#include "glm/vec3.hpp"
#include "glm/vec4.hpp"

struct alignas(16) ViewFrustum
{
    glm::vec4 Planes[6];
    glm::vec4& operator[](const size_t idx)
    {
        return Planes[idx];
    }
    const glm::vec4& operator[](const size_t idx) const
    {
        return Planes[idx];
    }
private:
    glm::vec4 Padding[2];
};

[[nodiscard]] bool PointInsideFrustum(const ViewFrustum& viewFrustum, const glm::vec4& testPt) noexcept;

struct alignas(16) AABB
{
    glm::vec4 Min = glm::vec4{ 
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
        0.0f 
    };
    glm::vec4 Max = glm::vec4{
        -std::numeric_limits<float>::max(),
        -std::numeric_limits<float>::max(),
        -std::numeric_limits<float>::max(),
        0.0f
    };
};

[[nodiscard]] glm::vec4 aabbExtent(const AABB& aabb) noexcept;
[[nodiscard]] glm::vec4 aabbCenter(const AABB& aabb) noexcept;
[[nodiscard]] bool aabbOverlap(const AABB& aabb0, const AABB& aabb1) noexcept;
void aabbIncludePosition(AABB& aabb, const glm::vec3& vec);

struct alignas(16) Sphere
{
    glm::vec3 Position;
    float Radius;
};

[[nodiscard]] bool SphereCoincidesWithAABB(const Sphere& sph, const AABB& aabb) noexcept;
