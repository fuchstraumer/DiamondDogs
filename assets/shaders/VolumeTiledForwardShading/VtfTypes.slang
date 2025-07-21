implementing VolumeTiledForwardShading;

// 64 bytes
public struct PointLight
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
public struct SpotLight
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
public struct DirectionalLight
{
    float4 Direction;
    float4 DirectionViewSpace;
    float3 Color;
    float Intensity;
    bool Enabled;
    float3 Padding;
}

public struct ClusterData
{
    uint3 GridDim;
    float ViewNear;
    uint2 ScreenSize;
    float NearK;
    float LogGridDimY;
}

public struct LightCounts
{
    uint NumPointLights;
    uint NumSpotLights;
    uint NumDirectionalLights;
    uint Padding;
}

internal struct BVHParams
{
    uint PointLightLevels;
    uint SpotLightLevels;
    uint ChildLevel;
    uint Padding;
}