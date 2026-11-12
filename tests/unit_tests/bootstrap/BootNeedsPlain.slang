module BootNeedsPlain;
import LodestoneAttributes;

// bootstrap_tests reads this module's manifest with no device: one variant, an 8 x 8 x 2 workgroup, push constants,
// and 64-bit integer math, so that BootNeedsWave and this module both need Int64. The code itself never runs.

RWStructuredBuffer<uint> Output;

[shader("compute")]
[numthreads(8, 8, 2)]
void PlainFillCS(uint3 threadId : SV_DispatchThreadID, uniform uint seed, uniform uint stride)
{
    const uint64_t wide = uint64_t(seed) * uint64_t(threadId.x + 1u);
    Output[threadId.x + threadId.y * stride + threadId.z * stride * 8u] = uint(wide >> 16u);
}
