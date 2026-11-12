module BootNeedsWave;
import LodestoneAttributes;

// bootstrap_tests reads this module's manifest with no device. The capability axis makes two variants: the wave
// variant needs the subgroup arithmetic capabilities, and the other does not. So those capabilities are Wanted. Both
// variants do 64-bit integer math, so Int64 is Required. The code itself never runs.

[ls_axis_boolean][ls_axis_kind("capability")]
extern static const bool NEEDS_USE_WAVE_OPS = true;

RWStructuredBuffer<uint> Output;

// 64 uints: 256 bytes of workgroup memory in both variants
groupshared uint s_Scratch[64];

[shader("compute")]
[numthreads(64, 1, 1)]
void WaveSumCS(uint3 threadId : SV_DispatchThreadID, uint groupIndex : SV_GroupIndex)
{
    // 64-bit math in both variants
    const uint64_t wide = uint64_t(threadId.x) * 0x9E3779B97F4A7C15ull;
    uint value = uint(wide >> 32u);

    if (NEEDS_USE_WAVE_OPS)
    {
        value = WaveActiveSum(value);
    }

    s_Scratch[groupIndex] = value;
    GroupMemoryBarrierWithGroupSync();
    Output[threadId.x] = s_Scratch[63u - groupIndex];
}
