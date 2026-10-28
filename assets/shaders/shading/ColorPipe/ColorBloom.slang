module ColorBloom;
import ddColor;

public struct BloomDownsamplePushConstants
{
    public float2 TexelSize;
    public uint UseKarisAverage;
    public float Padding;
};

public struct BloomUpsamplePushConstants
{
    public float2 CoarseTexelSize;
    public float Scatter;
    public float Padding;
};

SamplerState LinearSampler;

// Downsample bindings
Texture2D<float4> SourceMip;
RWTexture2D<float4> DestMip;

// Upsample bindings
Texture2D<float4> CoarseMip;
Texture2D<float4> FineMip;

[shader("compute")]
[numthreads(8, 8, 1)]
void ColorBloomDownsampleCS(uint3 dispatchThreadId : SV_DispatchThreadID, uniform BloomDownsamplePushConstants Push)
{
    uint outWidth, outHeight;
    DestMip.GetDimensions(outWidth, outHeight);
    if (dispatchThreadId.x >= outWidth || dispatchThreadId.y >= outHeight)
    {
        return;
    }

    const float2 uv = (float2(dispatchThreadId.xy) + 0.5) / float2(outWidth, outHeight);
    const float2 d = Push.TexelSize;

    // 13-tap Jimenez filter pattern (Jimenez 2014)
    const float3 a = SourceMip.SampleLevel(LinearSampler, uv + float2(-2.0, -2.0) * d, 0.0).rgb;
    const float3 b = SourceMip.SampleLevel(LinearSampler, uv + float2( 0.0, -2.0) * d, 0.0).rgb;
    const float3 c = SourceMip.SampleLevel(LinearSampler, uv + float2( 2.0, -2.0) * d, 0.0).rgb;

    const float3 d_tap = SourceMip.SampleLevel(LinearSampler, uv + float2(-2.0,  0.0) * d, 0.0).rgb;
    const float3 e     = SourceMip.SampleLevel(LinearSampler, uv + float2( 0.0,  0.0) * d, 0.0).rgb;
    const float3 f     = SourceMip.SampleLevel(LinearSampler, uv + float2( 2.0,  0.0) * d, 0.0).rgb;

    const float3 g = SourceMip.SampleLevel(LinearSampler, uv + float2(-2.0,  2.0) * d, 0.0).rgb;
    const float3 h = SourceMip.SampleLevel(LinearSampler, uv + float2( 0.0,  2.0) * d, 0.0).rgb;
    const float3 i = SourceMip.SampleLevel(LinearSampler, uv + float2( 2.0,  2.0) * d, 0.0).rgb;

    const float3 j = SourceMip.SampleLevel(LinearSampler, uv + float2(-1.0, -1.0) * d, 0.0).rgb;
    const float3 k = SourceMip.SampleLevel(LinearSampler, uv + float2( 1.0, -1.0) * d, 0.0).rgb;
    const float3 l = SourceMip.SampleLevel(LinearSampler, uv + float2(-1.0,  1.0) * d, 0.0).rgb;
    const float3 m = SourceMip.SampleLevel(LinearSampler, uv + float2( 1.0,  1.0) * d, 0.0).rgb;

    float3 downsample = 0.0;
    if (Push.UseKarisAverage != 0u)
    {
        float3 groups[5];
        groups[0] = (a + b + d_tap + e) * 0.25;
        groups[1] = (b + c + e + f) * 0.25;
        groups[2] = (d_tap + e + g + h) * 0.25;
        groups[3] = (e + f + h + i) * 0.25;
        groups[4] = (j + k + l + m) * 0.25;

        float weights[5] = { 0.125, 0.125, 0.125, 0.125, 0.5 };
        float3 sum = 0.0;
        float totalW = 0.0;
        for (int idx = 0; idx < 5; ++idx)
        {
            const float luma = Ap1Luminance(groups[idx]);
            const float w = weights[idx] / (1.0 + luma);
            sum += groups[idx] * w;
            totalW += w;
        }
        downsample = sum / max(totalW, 1.0e-6);
    }
    else
    {
        downsample = e * 0.125;
        downsample += (j + k + l + m) * 0.125;
        downsample += (a + c + g + i) * 0.03125;
        downsample += (b + d_tap + f + h) * 0.0625;
    }

    DestMip[dispatchThreadId.xy] = float4(downsample, 1.0);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void ColorBloomUpsampleCS(uint3 dispatchThreadId : SV_DispatchThreadID, uniform BloomUpsamplePushConstants Push)
{
    uint outWidth, outHeight;
    DestMip.GetDimensions(outWidth, outHeight);
    if (dispatchThreadId.x >= outWidth || dispatchThreadId.y >= outHeight)
    {
        return;
    }

    const float2 uv = (float2(dispatchThreadId.xy) + 0.5) / float2(outWidth, outHeight);
    const float2 d = Push.CoarseTexelSize;

    // 3x3 tent filter
    const float3 a = CoarseMip.SampleLevel(LinearSampler, uv + float2(-1.0, -1.0) * d, 0.0).rgb;
    const float3 b = CoarseMip.SampleLevel(LinearSampler, uv + float2( 0.0, -1.0) * d, 0.0).rgb;
    const float3 c = CoarseMip.SampleLevel(LinearSampler, uv + float2( 1.0, -1.0) * d, 0.0).rgb;

    const float3 d_tap = CoarseMip.SampleLevel(LinearSampler, uv + float2(-1.0,  0.0) * d, 0.0).rgb;
    const float3 e     = CoarseMip.SampleLevel(LinearSampler, uv + float2( 0.0,  0.0) * d, 0.0).rgb;
    const float3 f     = CoarseMip.SampleLevel(LinearSampler, uv + float2( 1.0,  0.0) * d, 0.0).rgb;

    const float3 g = CoarseMip.SampleLevel(LinearSampler, uv + float2(-1.0,  1.0) * d, 0.0).rgb;
    const float3 h = CoarseMip.SampleLevel(LinearSampler, uv + float2( 0.0,  1.0) * d, 0.0).rgb;
    const float3 i = CoarseMip.SampleLevel(LinearSampler, uv + float2( 1.0,  1.0) * d, 0.0).rgb;

    const float3 upsampled = (e * 4.0 + (b + d_tap + f + h) * 2.0 + (a + c + g + i)) * (1.0 / 16.0);
    const float3 finer = FineMip.Load(int3(dispatchThreadId.xy, 0)).rgb;

    const float3 blended = lerp(finer, upsampled, Push.Scatter);
    DestMip[dispatchThreadId.xy] = float4(blended, 1.0);
}
