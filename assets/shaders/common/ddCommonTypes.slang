implementing ddCommon;

public struct Cone
{
    public float3 TipPosition;
    public float Height;
    public float3 Direction;
    public float Radius;
}

public struct Frustum
{
    public float4 Planes[6];
}

public struct AABB
{
    public float3 Min;
    private float Pad0;
    public float3 Max;
    private float Pad1;
}

public struct DrawIndirectArgs
{
    public uint VertexCount;
    public uint InstanceCount;
    public uint FirstVertex;
    public uint FirstInstance;
}

public struct ComputeIndirectArgs
{
    public uint GroupCountX;
    public uint GroupCountY;
    public uint GroupCountZ;
    internal uint Padding;
}

public struct DrawIndexedIndirectArgs
{
    public uint IndexCountPerInstance;
    public uint InstanceCount;
    public uint FirstIndex;
    public int VertexOffset; // signed to allow negative offsets
    public uint FirstInstance;
}

// Core common view and projection matrices + their inverses for use across all shaders in this codebase
public struct CommonMatrices
{
    public float4x4 viewMatrix;
    public float4x4 projectionMatrix;
    public float4x4 viewProjectionMatrix;
    public float4x4 inverseViewMatrix;
    public float4x4 inverseProjectionMatrix;
    public float4x4 inverseViewProjectionMatrix;
}
