module LoomResolveOther;
import LodestoneAttributes;

// The second module of Loom's Resolve test bundle. It declares none of LoomResolveTest's axes, so a graph pin on TAPS
// applies to one module and is skipped for this one. A graph pin that neither module declares is LM206.

[ls_axis_boolean][ls_axis_kind("technique")]
extern static const bool INVERT = false;

RWStructuredBuffer<float> Values;

[shader("compute")]
[numthreads(64, 1, 1)]
void OtherCS(uint3 threadId : SV_DispatchThreadID)
{
    float value = Values[threadId.x];

    if (INVERT)
    {
        value = -value;
    }

    Values[threadId.x] = value;
}
