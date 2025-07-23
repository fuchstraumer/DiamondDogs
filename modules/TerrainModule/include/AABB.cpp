#include "AABB.hpp"
#include "glm/gtc/matrix_transform.hpp"

bool PointInsidePlane(const glm::vec4& pos, const glm::vec4& plane)
{
    const glm::vec3* planeNormal = reinterpret_cast<const glm::vec3*>(&plane);
    const glm::vec3* posVec3 = reinterpret_cast<const glm::vec3*>(&pos);
    return glm::dot(*planeNormal, *posVec3) - plane.w < 0.0f;
}

bool PointInsideFrustum(const ViewFrustum& viewFrustum, const glm::vec4& testPt) noexcept
{
    bool result = false;
    for (size_t i = 0; i < 6; ++i)
    {
        result |= PointInsidePlane(testPt, viewFrustum[i]);
    }
    return result;
}

glm::vec4 aabbExtent(const AABB& aabb) noexcept
{
    return glm::abs(aabb.Max - aabb.Min);
}

glm::vec4 aabbCenter(const AABB& aabb) noexcept
{
    return (aabb.Min + aabb.Max) / glm::vec4(2.0f);
}

void aabbIncludePosition(AABB& aabb, const glm::vec3& vec)
{
    glm::vec4 vec4_in(vec, 0.0f);
    aabb.Min = glm::min(vec4_in, aabb.Min);
    aabb.Max = glm::max(vec4_in, aabb.Max);
}

glm::vec4 when_lt(const glm::vec4& x, const glm::vec4& y)
{
    return glm::max(glm::sign(y - x), glm::vec4(0.0f));
}

glm::vec4 when_gt(const glm::vec4& x, const glm::vec4& y)
{
    return glm::max(glm::sign(x - y), glm::vec4(0.0f));
}

bool SphereCoincidesWithAABB(const Sphere& sph, const AABB& aabb) noexcept
{
    glm::vec3 result(0.0f, 0.0f, 0.0f);
    glm::vec4 centerTmp(sph.Position.x, sph.Position.y, sph.Position.z, 0.0f);
    result += when_lt(centerTmp, aabb.Min) * (centerTmp - aabb.Min) * (centerTmp - aabb.Min);
    result += when_gt(centerTmp, aabb.Max) * (centerTmp - aabb.Max) * (centerTmp - aabb.Max);
    return (result.x + result.y + result.z) <= (sph.Radius * sph.Radius);
}
