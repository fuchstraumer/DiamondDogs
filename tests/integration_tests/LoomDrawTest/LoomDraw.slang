module LoomDraw;
import LoomFrame;

// Loom step 7: the draws of loom_tests (no GPU) and of LoomDrawTest (headless, on a GPU). Vertices are pulled from
// Positions (no vertex input). The frame: CullCS writes the indirect arguments and the draw count; a depth-only
// prepass (SceneVS with no fragment shader); GBufferPS writes two color targets and tests depth Equal; LightPS reads
// both and LoomFrame, and writes the frame target.

struct VertexOut
{
    float4 Position : SV_Position;
    float2 Uv : TEXCOORD0;
};

// xyz is the clip-space position (w is 1); the triangles are flat, so depth is z
StructuredBuffer<float4> Positions;

[shader("vertex")]
VertexOut SceneVS(uint vertexId : SV_VertexID)
{
    const float4 position = Positions[vertexId];
    VertexOut result;
    result.Position = float4(position.xyz, 1.0f);
    result.Uv = position.xy * 0.5f + 0.5f;
    return result;
}

struct GBufferOut
{
    float4 Albedo : SV_Target0;
    float4 Normal : SV_Target1;
};

[shader("fragment")]
GBufferOut GBufferPS(VertexOut input)
{
    GBufferOut result;
    result.Albedo = float4(input.Uv, 0.25f, 1.0f);
    result.Normal = float4(0.0f, 0.0f, 1.0f, input.Position.z);
    return result;
}

// One triangle over the whole target, from SV_VertexID. NDC is Y-up and texture v is down, so v = 1 - corner.y: a
// pixel samples the texel at its own place.
[shader("vertex")]
VertexOut FullscreenVS(uint vertexId : SV_VertexID)
{
    const float2 corner = float2(float((vertexId << 1u) & 2u), float(vertexId & 2u));
    VertexOut result;
    result.Position = float4(corner * 2.0f - 1.0f, 0.0f, 1.0f);
    result.Uv = float2(corner.x, 1.0f - corner.y);
    return result;
}

Texture2D<float4> Albedo;
Texture2D<float4> Normal;
SamplerState PointSampler;

[shader("fragment")]
float4 LightPS(VertexOut input) : SV_Target0
{
    // the scene images may be larger than the render extent (a resize that has not settled)
    const float2 uv = input.Uv * g_LoomFrame.Targets[0].RenderToAllocation;
    const float4 albedo = Albedo.Sample(PointSampler, uv);
    const float4 normal = Normal.Sample(PointSampler, uv);
    return float4(albedo.rgb * normal.z, 1.0f);
}

// GPU culling, reduced to its outputs: DrawCount draws of 3 vertices. Each draw is 4 uints (vertex count, instance
// count, first vertex, first instance), packed tightly.
RWStructuredBuffer<uint> DrawArguments;
RWStructuredBuffer<uint> DrawCount;

[shader("compute")]
[numthreads(1, 1, 1)]
void CullCS(uint3 threadId : SV_DispatchThreadID)
{
    DrawArguments[0] = 3u;
    DrawArguments[1] = 1u;
    DrawArguments[2] = 3u;
    DrawArguments[3] = 0u;
    DrawCount[0] = 1u;
}
