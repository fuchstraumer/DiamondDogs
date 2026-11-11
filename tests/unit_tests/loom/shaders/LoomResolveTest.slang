module LoomResolveTest;
import LodestoneAttributes;

// Loom's Resolve tests read this module's manifest with no device. Each axis domain and kind that Resolve handles is
// here once: a technique boolean, a tuning integer, an enum, an interface, and a capability boolean with a tuning
// axis gated on it. The compute entry point reads DOUBLE_PASS, TAPS, USE_WAVE_OPS and WAVE_SIZE; the fragment entry
// point reads SHADE and CURVE; the vertex entry point reads none. So the tests also see axis relevance.

public enum ToneCurve
{
    Linear,
    Filmic,
};

public interface IShade
{
    float4 Shade(float4 color);
}

[ls_axis_interface_impl("IShade")]
public struct ShadeFlat : IShade
{
    public float4 Shade(float4 color)
    {
        return color;
    }
}

[ls_axis_interface_impl("IShade")]
public struct ShadeWarm : IShade
{
    public float4 Shade(float4 color)
    {
        return color * float4(1.1f, 1.0f, 0.9f, 1.0f);
    }
}

[ls_axis_interface]
public extern struct SHADE : IShade = ShadeFlat;

[ls_axis_boolean][ls_axis_kind("technique")]
extern static const bool DOUBLE_PASS = false;

[ls_axis_values("1, 2, 4")][ls_axis_kind("tuning")]
extern static const uint TAPS = 1u;

[ls_axis_enum][ls_axis_kind("technique")]
extern static const ToneCurve CURVE = ToneCurve::Linear;

[ls_axis_boolean][ls_axis_kind("capability")]
extern static const bool USE_WAVE_OPS = false;

[ls_axis_values("16, 32")][ls_axis_kind("tuning")][ls_axis_active_when("USE_WAVE_OPS==1")]
extern static const uint WAVE_SIZE = 32u;

struct ResolveParams
{
    uint Count;
    float Scale;
};

ConstantBuffer<ResolveParams> Params;
RWStructuredBuffer<float> Output;

[shader("compute")]
[numthreads(64, 1, 1)]
void ResolveCS(uint3 threadId : SV_DispatchThreadID)
{
    if (threadId.x >= Params.Count)
    {
        return;
    }

    float value = float(threadId.x) * Params.Scale;

    for (uint tap = 0u; tap < TAPS; ++tap)
    {
        value += float(tap);
    }

    if (DOUBLE_PASS)
    {
        value *= 2.0f;
    }

    if (USE_WAVE_OPS)
    {
        value = WaveActiveSum(value) / float(WAVE_SIZE);
    }

    Output[threadId.x] = value;
}

struct VertexOut
{
    float4 Position : SV_Position;
    float4 Color : COLOR0;
};

[shader("vertex")]
VertexOut ResolveVS(uint vertexId : SV_VertexID)
{
    const float2 corner = float2(float((vertexId << 1u) & 2u), float(vertexId & 2u));
    VertexOut result;
    result.Position = float4(corner * 2.0f - 1.0f, 0.0f, 1.0f);
    result.Color = float4(corner, 0.5f, 1.0f);
    return result;
}

[shader("fragment")]
float4 ResolvePS(VertexOut input) : SV_Target
{
    SHADE shade = {};
    float4 color = shade.Shade(input.Color);

    if (CURVE == ToneCurve::Filmic)
    {
        color = color / (color + 1.0f);
    }

    return color;
}
