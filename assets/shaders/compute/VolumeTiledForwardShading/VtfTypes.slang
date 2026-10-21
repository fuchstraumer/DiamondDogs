implementing VolumeTiledForwardShading;

// 64 bytes
public struct PointLight
{
    float4 Position;
    float4 PositionViewSpace;
    float3 Color;
    float Range;
    float Intensity;
    uint Enabled;
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
    float SpotLightAngle;
    float Range;
    float Intensity;
    uint Enabled;
    // fix: pad to 96 bytes
    float Padding;
}

// 64 bytes
public struct DirectionalLight
{
    float4 Direction;
    float4 DirectionViewSpace;
    float3 Color;
    float Intensity;
    uint Enabled;
    float3 Padding;
}

public struct ClusterData
{
    uint3 GridDim;
    float ViewNear;
    uint2 ClusterSizeInPixels;
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

public struct BVHParams
{
    uint PointLightLevels;
    uint SpotLightLevels;
    uint ChildLevel;
    uint Padding;
}

// Parameters that the reduction pass reads. This was a push constant in the GLSL.
public struct ReduceLightAABBParams
{
    uint NumThreadGroupsX;
    uint ReductionNumElements;
    uint2 Padding;
}

// Parameters that both sort passes read.
public struct SortParams
{
    uint NumElements;
    uint ChunkSize;
    uint2 Padding;
}
