implementing ddCommon;

// 15 digits of pi since that's within bounds of floating point error (though, this may differ in shaders?)
internal const static float PI = 3.14159265358979f;

public float3 ExpandNormal(in float3 normal)
{
    return normal * float3(2.0f) - float3(1.0f);
}

public float4 DoNormalMapping(in float3 normal, in float3x3 TBN)
{
    float3 expandedNormal = ExpandNormal(normal);
    float3 transformedNormal = mul(TBN, expandedNormal);
    return normalize(float4(transformedNormal, 0.0f));
}

// Returns TBN matrix from normal and tangent vectors. Input vectors must be normalized
public float3x3 MakeTBN(in const float3 normal, in const float3 tangent)
{
    const float3 bitangent = cross(normal, tangent);
    return float3x3(tangent, bitangent, normal);
}

public float Attenuate(in float range, in float distance)
{
    return 1.0f - smoothstep(range * 0.75f, range, distance);
}

public float DistributionGGX(in float3 normal, in float3 H, in float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;

    float NdotH = max(dot(normal, H), 0.0f);
    float NdotH2 = NdotH * NdotH;

    float denom = (NdotH2 * (a2 - 1.0f) + 1.0f);
    denom = PI * denom * denom;

    return a2 / denom;
}

public float GeometrySchlickGGX(in const float NdotV, in const float roughness)
{
    const float k_ratio = 0.125f; // 1.0f / 8.0f
    float r = roughness + 1.0f;
    float k = k_ratio * r * r;
    return NdotV / (NdotV * (1.0f - k) + k);
}

public float FresnelSchlick(in const float cosTheta, in const float3 F0)
{
    return F0.x + (1.0f - F0.x) * pow(1.0f - cosTheta, 5.0f);
}

// Applying inverse projection matrix "undoes" the projection transform, converting clip space coordinates to view space coordinates.
public float4 ClipToView(in const float4 clip_position, in const float4x4 inverse_projection_matrix)
{
    float4 view_position = mul(clip_position, inverse_projection_matrix);
    view_position /= view_position.w; // Perspective divide
    return view_position;
}

public float4 ScreenToView(in const float4 screen_position, in float2 screen_dimensions, in const float4x4 inverse_projection_matrix)
{
    float2 screen_coords = screen_position.xy / screen_dimensions;
    float4 clip_position = float4(screen_coords * 2.0f - 1.0f, screen_position.z, screen_position.w);
    return ClipToView(clip_position, inverse_projection_matrix);
}

// Recall that plane.w is signed distance from the origin to the plane along the normal, so this returns true 
// if the sphere is on the "inside" side of the plane (i.e., the side where the normal points, not if the sphere is literally inside the plane)
public bool SphereInsidePlane(in float4 sphere, in float4 plane)
{
    return dot(plane.xyz, sphere.xyz) + plane.w >= sphere.w;
}

// Fixed from previous version, where we did dot() - plane.w < 0.0f, which was assuming plane.w was absolute distance from origin to plane, not signed distance
public bool PointInsidePlane(in float3 point, in float4 plane)
{
    return dot(plane.xyz, point) + plane.w >= 0.0f;
}

// Returns float3(1.0f, 1.0f, 1.0f) if lhs < rhs, otherwise returns float3(0.0f, 0.0f, 0.0f)
public float3 WhenLessThan(in const float3 lhs, in const float3 rhs)
{
    return max(sign(rhs - lhs), float3(0.0f));
}

public float4 WhenLessThan(in const float4 lhs, in const float4 rhs)
{
    return max(sign(rhs - lhs), float4(0.0f));
}

// Returns float3(1.0f, 1.0f, 1.0f) if lhs > rhs, otherwise returns float3(0.0f, 0.0f, 0.0f)
public float3 WhenGreaterThan(in const float3 lhs, in const float3 rhs)
{
    return max(sign(lhs - rhs), float3(0.0f));
}

public float4 WhenGreaterThan(in const float4 lhs, in const float4 rhs)
{
    return max(sign(lhs - rhs), float4(0.0f));
}

// Returns float3(1.0f, 1.0f, 1.0f) if lhs <= rhs, otherwise returns float3(0.0f, 0.0f, 0.0f)
// (the 1.0f - WhenLessThan means even in the 0.0f case when sign() returns equality, we get 1.0f)
public float3 WhenLessThanOrEqual(in const float3 lhs, in const float3 rhs)
{
    return 1.0f - WhenLessThan(lhs, rhs);
}

public float4 WhenLessThanOrEqual(in const float4 lhs, in const float4 rhs)
{
    return 1.0f - WhenLessThan(lhs, rhs);
}

// Returns float3(1.0f, 1.0f, 1.0f) if lhs >= rhs, otherwise returns float3(0.0f, 0.0f, 0.0f)
public float3 WhenGreaterThanOrEqual(in const float3 lhs, in const float3 rhs)
{
    return 1.0f - WhenGreaterThan(lhs, rhs);
}

public float4 WhenGreaterThanOrEqual(in const float4 lhs, in const float4 rhs)
{
    return 1.0f - WhenGreaterThan(lhs, rhs);
}

// Branchless intersection test for sphere and AABB. Finds squared distance from sphere center to AABB, and checks if it's less than squared radius.
// Sphere is represented as float4 (x, y, z, radius), AABB as two float4s (min, max)
public bool SphereInsideAABBFast(in const float4 sphere, in const float3 aabb_min, in const float3 aabb_max)
{
    float3 result = float3(0.0f);
    result += WhenLessThan(sphere.xyz, aabb_min) * (sphere.xyz - aabb_min) * (sphere.xyz - aabb_min);
    result += WhenGreaterThan(sphere.xyz, aabb_max) * (sphere.xyz - aabb_max) * (sphere.xyz - aabb_max);
    return (result.x + result.y + result.z) <= (sphere.w * sphere.w);
}

public bool AABBIntersectAABB(in const float3 aMin, in const float3 aMax, in const float3 bMin, in const float3 bMax)
{
    return all(aMax >= bMin) && all(aMin <= bMax);
}