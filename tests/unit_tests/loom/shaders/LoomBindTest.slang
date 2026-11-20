module LoomBindTest;
import LodestoneAttributes;
import LodestoneShims;

// Loom's Bind tests read this module's manifest with no device. BindCS has one binding of each kind that a dispatch
// binds by name: a read-only buffer, a read-write buffer, a uniform block and a texture with its sampler. Scratch is
// read only when USE_SCRATCH is on, so it is in some variants only. Weights is a shim: a bound buffer on spirv, and a
// buffer device address in the push constants on spirv-modern-mutable. The draw pair reads Tints in both stages, so
// the merged layout gives it both stage bits.

[ls_axis_boolean][ls_axis_kind("technique")]
extern static const bool USE_SCRATCH = false;

public struct Weight
{
    public float value;
};

extern struct WeightBuffer : IReadBuffer<Weight>;

struct BindSettings
{
    float Bias;
    uint Stride;
};

StructuredBuffer<float> Input;
RWStructuredBuffer<float> Output;
RWStructuredBuffer<float> Scratch;
ConstantBuffer<BindSettings> Settings;
Texture2D<float4> Albedo;
SamplerState Linear;
StructuredBuffer<float4> Tints;

[shader("compute")]
[numthreads(64, 1, 1)]
void BindCS(uint3 threadId : SV_DispatchThreadID, uniform float scale, uniform uint count, uniform WeightBuffer weights)
{
    if (threadId.x >= count)
    {
        return;
    }

    const float2 uv = float2(float(threadId.x) / float(count), 0.5f);
    float value = Input[threadId.x * Settings.Stride] * scale + Settings.Bias;
    value += weights.Load(threadId.x).value;
    value += Albedo.SampleLevel(Linear, uv, 0.0f).x;

    if (USE_SCRATCH)
    {
        Scratch[threadId.x] = value;
        value += Scratch[threadId.x ^ 1u];
    }

    Output[threadId.x] = value;
}

struct VertexOut
{
    float4 Position : SV_Position;
    float2 Uv : TEXCOORD0;
    float4 Tint : COLOR0;
};

[shader("vertex")]
VertexOut BindVS(uint vertexId : SV_VertexID)
{
    const float2 corner = float2(float((vertexId << 1u) & 2u), float(vertexId & 2u));
    VertexOut result;
    result.Position = float4(corner * 2.0f - 1.0f, 0.0f, 1.0f);
    result.Uv = corner;
    result.Tint = Tints[0];
    return result;
}

[shader("fragment")]
float4 BindPS(VertexOut input) : SV_Target
{
    return Albedo.Sample(Linear, input.Uv) * input.Tint * Tints[1];
}
