module ColorBloom;
import LodestoneAttributes;
import LodestoneShims;
import ddColor;

[ls_axis_boolean][ls_axis_kind("Technique")]
extern static const bool USE_KARIS_AVERAGE = false;

public struct BloomDownsamplePushConstants
{
    public float2 TexelSize;
    public float2 Padding;
};

public struct BloomUpsamplePushConstants
{
    public float2 CoarseTexelSize;
    public float Scatter;
    public float Padding;
};

SamplerState LinearSampler;

// Downsample bindings
[ls_extent_2d("1024", "1024")]
Texture2D<float4> SourceMip;
[format("rgba16f")]
[ls_extent_2d("1024", "1024")]
RWTexture2D<float4> DestMip;

// Upsample bindings
[ls_extent_2d("1024", "1024")]
Texture2D<float4> CoarseMip;
[ls_extent_2d("1024", "1024")]
Texture2D<float4> FineMip;

[shader("compute")]
[numthreads(8, 8, 1)]
void ColorBloomDownsampleCS(uint3 dispatchThreadId : SV_DispatchThreadID, uniform BloomDownsamplePushConstants push)
{
    uint outWidth;
    uint outHeight;
    DestMip.GetDimensions(outWidth, outHeight);
    if (dispatchThreadId.x >= outWidth || dispatchThreadId.y >= outHeight)
    {
        return;
    }

    const float2 uv = (float2(dispatchThreadId.xy) + 0.5f) / float2(float(outWidth), float(outHeight));
    const float2 d = push.TexelSize;

    // 13-tap Jimenez filter pattern (Jimenez 2014)
    const float3 a = SourceMip.SampleLevel(LinearSampler, uv + float2(-2.0f, -2.0f) * d, 0.0f).rgb;
    const float3 b = SourceMip.SampleLevel(LinearSampler, uv + float2( 0.0f, -2.0f) * d, 0.0f).rgb;
    const float3 c = SourceMip.SampleLevel(LinearSampler, uv + float2( 2.0f, -2.0f) * d, 0.0f).rgb;

    const float3 dTap = SourceMip.SampleLevel(LinearSampler, uv + float2(-2.0f,  0.0f) * d, 0.0f).rgb;
    const float3 e    = SourceMip.SampleLevel(LinearSampler, uv + float2( 0.0f,  0.0f) * d, 0.0f).rgb;
    const float3 f    = SourceMip.SampleLevel(LinearSampler, uv + float2( 2.0f,  0.0f) * d, 0.0f).rgb;

    const float3 g = SourceMip.SampleLevel(LinearSampler, uv + float2(-2.0f,  2.0f) * d, 0.0f).rgb;
    const float3 h = SourceMip.SampleLevel(LinearSampler, uv + float2( 0.0f,  2.0f) * d, 0.0f).rgb;
    const float3 i = SourceMip.SampleLevel(LinearSampler, uv + float2( 2.0f,  2.0f) * d, 0.0f).rgb;

    const float3 j = SourceMip.SampleLevel(LinearSampler, uv + float2(-1.0f, -1.0f) * d, 0.0f).rgb;
    const float3 k = SourceMip.SampleLevel(LinearSampler, uv + float2( 1.0f, -1.0f) * d, 0.0f).rgb;
    const float3 l = SourceMip.SampleLevel(LinearSampler, uv + float2(-1.0f,  1.0f) * d, 0.0f).rgb;
    const float3 m = SourceMip.SampleLevel(LinearSampler, uv + float2( 1.0f,  1.0f) * d, 0.0f).rgb;

    float3 downsample = float3(0.0f);
    if (USE_KARIS_AVERAGE)
    {
        float3 groups[5];
        groups[0] = (a + b + dTap + e) * 0.25f;
        groups[1] = (b + c + e + f) * 0.25f;
        groups[2] = (dTap + e + g + h) * 0.25f;
        groups[3] = (e + f + h + i) * 0.25f;
        groups[4] = (j + k + l + m) * 0.25f;

        const float weights[5] = { 0.125f, 0.125f, 0.125f, 0.125f, 0.5f };
        float3 sum = float3(0.0f);
        float totalWeight = 0.0f;
        [unroll]
        for (int idx = 0; idx < 5; ++idx)
        {
            const float luma = Ap1Luminance(groups[idx]);
            const float w = weights[idx] / (1.0f + luma);
            sum += groups[idx] * w;
            totalWeight += w;
        }
        downsample = sum / max(totalWeight, 1.0e-6f);
    }
    else
    {
        downsample = e * 0.125f;
        downsample += (j + k + l + m) * 0.125f;
        downsample += (a + c + g + i) * 0.03125f;
        downsample += (b + dTap + f + h) * 0.0625f;
    }

    DestMip[dispatchThreadId.xy] = float4(downsample, 1.0f);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void ColorBloomUpsampleCS(uint3 dispatchThreadId : SV_DispatchThreadID, uniform BloomUpsamplePushConstants push)
{
    uint outWidth;
    uint outHeight;
    DestMip.GetDimensions(outWidth, outHeight);
    if (dispatchThreadId.x >= outWidth || dispatchThreadId.y >= outHeight)
    {
        return;
    }

    const float2 uv = (float2(dispatchThreadId.xy) + 0.5f) / float2(float(outWidth), float(outHeight));
    const float2 d = push.CoarseTexelSize;

    // 3x3 tent filter
    const float3 a = CoarseMip.SampleLevel(LinearSampler, uv + float2(-1.0f, -1.0f) * d, 0.0f).rgb;
    const float3 b = CoarseMip.SampleLevel(LinearSampler, uv + float2( 0.0f, -1.0f) * d, 0.0f).rgb;
    const float3 c = CoarseMip.SampleLevel(LinearSampler, uv + float2( 1.0f, -1.0f) * d, 0.0f).rgb;

    const float3 dTap = CoarseMip.SampleLevel(LinearSampler, uv + float2(-1.0f,  0.0f) * d, 0.0f).rgb;
    const float3 e    = CoarseMip.SampleLevel(LinearSampler, uv + float2( 0.0f,  0.0f) * d, 0.0f).rgb;
    const float3 f    = CoarseMip.SampleLevel(LinearSampler, uv + float2( 1.0f,  0.0f) * d, 0.0f).rgb;

    const float3 g = CoarseMip.SampleLevel(LinearSampler, uv + float2(-1.0f,  1.0f) * d, 0.0f).rgb;
    const float3 h = CoarseMip.SampleLevel(LinearSampler, uv + float2( 0.0f,  1.0f) * d, 0.0f).rgb;
    const float3 i = CoarseMip.SampleLevel(LinearSampler, uv + float2( 1.0f,  1.0f) * d, 0.0f).rgb;

    const float3 upsampled = (e * 4.0f + (b + dTap + f + h) * 2.0f + (a + c + g + i)) * (1.0f / 16.0f);
    const float3 finer = FineMip.Load(int3(dispatchThreadId.xy, 0)).rgb;

    const float3 blended = lerp(finer, upsampled, push.Scatter);
    DestMip[dispatchThreadId.xy] = float4(blended, 1.0f);
}
