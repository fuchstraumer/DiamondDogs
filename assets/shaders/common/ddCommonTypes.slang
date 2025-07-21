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
