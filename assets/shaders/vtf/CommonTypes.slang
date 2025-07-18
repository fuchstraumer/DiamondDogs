
// 64 bytes
struct PointLight
{
    float4 Position;
    float4 PositionViewSpace;
    float3 Color;
    float Range;
    float Intensity;
    bool Enabled;
    float2 Padding;
}

// 96 bytes
struct SpotLight
{
    float4 Position;
    float4 PositionViewSpace;
    float4 Direction;
    float4 DirectionViewSpace;
    float3 Color;
    float Range;
    float Intensity;
    bool Enabled;
    float Padding;
}

// 64 bytes
struct DirectionalLight
{
    float4 Direction;
    float4 DirectionViewSpace;
    float3 Color;
    float Intensity;
    bool Enabled;
    float3 Padding;
}

struct AABB
{
    float4 Min;
    float4 Max;
}

struct Plane
{
    float3 Normal;
    float Distance;
}

struct Frustum
{
    Plane Planes[6];
    float4 Corners[8]; // 8 corners of the frustum
}

struct Sphere
{
    float3 Center;
    float Radius;
}

struct LightingResult
{
    float3 Diffuse;
    float Pad0;
    float3 Specular;
    float Pad1;
}

struct SurfaceOutput
{
    float3 Albedo;
    float Metallic;
    // Normal is in tangent space
    float3 Normal;
    float Roughness;
    float3 Emissive;
    float Occlusion;
    float Alpha;
    float3 Padding;
}