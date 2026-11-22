module LoomImages;
import LodestoneAttributes;

// Loom step 6 on the GPU (images, samplers, history rings, run policies), and the shaders of loom_tests' step 6 runtime
// tests. Every value is a small integer, in float or uint, so the comparison with the CPU is exact.
//
//  - PaintCS writes Painted, a storage image (General). SampleCS samples it in the next level (ShaderReadOnly), so
//    Loom must change its layout between them. SampleCS also samples Lut, an imported image, with a point sampler at
//    texel centers, and covers Lut's extent, which Loom learns only when Lut is supplied.
//  - AccumulateCS adds 1 to last frame's count, through BindPrevious on a history buffer. AccumulateImageCS does the
//    same on a history image, and CopyCountCS copies this frame's count image into a buffer to read back.
//  - ChangedCS adds its params value to Changed when it runs (WhenInputsChange); TriggeredCS adds 1 to Triggered
//    (OnTrigger). The sums tell how many times each ran.

[format("rgba32f")]
RWTexture2D<float4> Painted;
Texture2D<float4> PaintedRead;
Texture2D<float4> Lut;
SamplerState PointSampler;
RWStructuredBuffer<float4> Sampled;

StructuredBuffer<uint> PrevCount;
RWStructuredBuffer<uint> NextCount;

Texture2D<uint> PrevCountImage;
[format("r32ui")]
RWTexture2D<uint> NextCountImage;
Texture2D<uint> CountImageRead;
RWStructuredBuffer<uint> CountImageOut;

RWStructuredBuffer<uint> Changed;
RWStructuredBuffer<uint> Triggered;

[shader("compute")]
[numthreads(8, 8, 1)]
void PaintCS(uint3 threadId : SV_DispatchThreadID, uniform uint seed)
{
    Painted[threadId.xy] = float4(float(threadId.x), float(threadId.y), float(seed), float(threadId.x * threadId.y));
}

[shader("compute")]
[numthreads(8, 8, 1)]
void SampleCS(uint3 threadId : SV_DispatchThreadID, uniform uint width)
{
    uint lutWidth;
    uint lutHeight;
    Lut.GetDimensions(lutWidth, lutHeight);
    const float2 uv = (float2(threadId.xy) + 0.5f) / float2(lutWidth, lutHeight);
    const float4 painted = PaintedRead.SampleLevel(PointSampler, uv, 0.0f);
    const float4 lut = Lut.SampleLevel(PointSampler, uv, 0.0f);
    Sampled[threadId.y * width + threadId.x] = painted + lut;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void AccumulateCS(uint3 threadId : SV_DispatchThreadID, uniform uint count)
{
    if (threadId.x >= count)
    {
        return;
    }

    NextCount[threadId.x] = PrevCount[threadId.x] + 1u;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void AccumulateImageCS(uint3 threadId : SV_DispatchThreadID)
{
    NextCountImage[threadId.xy] = PrevCountImage.Load(int3(threadId.xy, 0)) + 1u;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void CopyCountCS(uint3 threadId : SV_DispatchThreadID, uniform uint width)
{
    CountImageOut[threadId.y * width + threadId.x] = CountImageRead.Load(int3(threadId.xy, 0));
}

[shader("compute")]
[numthreads(64, 1, 1)]
void ChangedCS(uint3 threadId : SV_DispatchThreadID, uniform uint count, uniform uint value)
{
    if (threadId.x >= count)
    {
        return;
    }

    Changed[threadId.x] = Changed[threadId.x] + value;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void TriggeredCS(uint3 threadId : SV_DispatchThreadID, uniform uint count)
{
    if (threadId.x >= count)
    {
        return;
    }

    Triggered[threadId.x] = Triggered[threadId.x] + 1u;
}
