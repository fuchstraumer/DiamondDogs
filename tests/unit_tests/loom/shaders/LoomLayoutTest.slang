module LoomLayoutTest;
import LodestoneAttributes;

// Loom's Layout tests. GridCS runs 8x8 groups over Grid, an rgba32f storage image, and reads Weights. The draws all use
// LayoutVS, which reads no vertex input (it makes a full-screen triangle from SV_VertexID). Their fragment shaders
// differ in their outputs: one float4, a float4 and a float2, one uint, or one float4 and the depth. MeshVS reads a
// POSITION vertex input, which Loom has no vertex format for (LM603).

[format("rgba32f")]
RWTexture2D<float4> Grid;

StructuredBuffer<float> Weights;

Texture2D<float4> Source;
SamplerState Linear;

[shader("compute")]
[numthreads(8, 8, 1)]
void GridCS(uint3 threadId : SV_DispatchThreadID)
{
    Grid[threadId.xy] = float4(Weights[threadId.x & 63u], 0.0f, 0.0f, 1.0f);
}

struct VertexOut
{
    float4 Position : SV_Position;
    float2 Uv : TEXCOORD0;
};

[shader("vertex")]
VertexOut LayoutVS(uint vertexId : SV_VertexID)
{
    const float2 corner = float2(float((vertexId << 1u) & 2u), float(vertexId & 2u));
    VertexOut result;
    result.Position = float4(corner * 2.0f - 1.0f, 0.0f, 1.0f);
    result.Uv = corner;
    return result;
}

[shader("vertex")]
float4 MeshVS(float3 position : POSITION) : SV_Position
{
    return float4(position, 1.0f);
}

// One float4 output at location 0, from Source
[shader("fragment")]
float4 ColorPS(VertexOut input) : SV_Target0
{
    return Source.Sample(Linear, input.Uv);
}

struct TwoOutputs
{
    float4 Color : SV_Target0;
    float2 Normal : SV_Target1;
};

[shader("fragment")]
TwoOutputs TwoPS(VertexOut input)
{
    TwoOutputs result;
    result.Color = float4(input.Uv, 0.0f, 1.0f);
    result.Normal = input.Uv;
    return result;
}

[shader("fragment")]
uint IdPS(VertexOut input) : SV_Target0
{
    return uint(input.Uv.x * 255.0f);
}

[shader("fragment")]
float4 DepthPS(VertexOut input, out float depth : SV_Depth) : SV_Target0
{
    depth = input.Uv.y;
    return float4(input.Uv, 0.0f, 1.0f);
}
