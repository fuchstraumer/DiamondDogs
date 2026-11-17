module LoomResolveOther;
import LodestoneAttributes;

// The second module of Loom's Resolve test bundle. It declares none of LoomResolveTest's axes, so a graph pin on TAPS
// applies to one module and is skipped for this one. A graph pin that neither module declares is LM206.
// SMOOTH and SMOOTH_TAPS are a technique gate pair with no capability, so no device rule picks the gate: both take
// their defaults in one round. The gate default is off, so the default of SMOOTH_TAPS must not switch it on.

[ls_axis_boolean][ls_axis_kind("technique")]
extern static const bool INVERT = false;

[ls_axis_boolean][ls_axis_kind("technique")]
extern static const bool SMOOTH = false;

[ls_axis_values("2, 4")][ls_axis_kind("tuning")][ls_axis_active_when("SMOOTH==1")]
extern static const uint SMOOTH_TAPS = 4u;

// Each host type that DiamondDogs maps (math/ShaderHostTypes.hpp), so the generated host header proves the mapping.
// std430: float4x4 packs to 64 bytes; float4x3 has float3 rows padded to 16 bytes, so the header pads it.
struct HostShapes
{
    float4x4 world;
    float3 position;
    uint flags;
    int3 cell;
    float weight;
    uint2 ids;
    float2 uv;
    float4 tint;
    float4x3 frame;
};

StructuredBuffer<HostShapes> Shapes;
RWStructuredBuffer<float> Values;

[shader("compute")]
[numthreads(64, 1, 1)]
void OtherCS(uint3 threadId : SV_DispatchThreadID)
{
    const HostShapes shape = Shapes[0];
    float value = Values[threadId.x] * shape.weight + shape.world[3].x + shape.frame[3].y + shape.position.z;
    value += float(shape.flags + shape.ids.y) + float(shape.cell.x) + shape.uv.y + shape.tint.w;

    if (INVERT)
    {
        value = -value;
    }

    if (SMOOTH)
    {
        value /= float(SMOOTH_TAPS);
    }

    Values[threadId.x] = value;
}
