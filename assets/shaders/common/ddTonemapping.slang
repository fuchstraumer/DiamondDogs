implementing ddCommon;

// Lottes tonemapping can be matched almost 1:1 with ACES by using the following parameters:
// Contrast: 1.6 HighlightContrast: 0.977 (ClippingPoint and CompressionSlope are computed based on these)
// HdrMax: 8.0 MidpointIn: 0.18 MidpointOut: 0.267
// ChannelCrosstalk: 4.0
public struct TonemapParams
{
    float Contrast;
    float HighlightContrast;
    float ClippingPoint;
    float CompressionSlope;
    float HdrMax;
    float MidpointIn;
    float MidpointOut;
    float ChannelCrosstalk;
};

internal float Reinhard(in const float3 inColor)
{
    return inColor / (inColor + 1.0f);
}

internal float LottesTonemapOperator(const float x, const float4 p)
{
    const float z = pow(x, p.x);
    return z / (pow(z, p.y) * p.z + p.w);
}

// Core tonemap function from T. Lottes GDC talk
// z = pow(x, a) -> where "a" is a term that sets the level of contrast or shapes the toe of the tonemapper curve (contrast function)
// zDivisor = (pow(z, b) * c) + d (highlight compressor)
// "b" term controls the clipping point, set based on the maximum brightness of the scene currently
// "c" term sets how rapidly compression applies, especially in the shoulder of the curve
// "d" term sets the highlight contrast, or the shape of the curve at the brightest parts
// bc are computed per-frame on the CPU using fixed properties and our contrast parameters
internal float3 LotteTonemap(in const float3 color, in uniform TonemapParams params)
{
    const float peakChannel = max(max(max(color.x, color.y), color.z), 1e-5);
    float3 ratio = color / peakChannel.xxx;
    const float4 pParam = float4(params.Contrast, params.HighlightContrast, params.ClippingPoint, params.CompressionSlope);
    const float tonemappedPeak = LottesTonemapOperator(peakChannel, pParam);

    const float crossSaturation = params.Contrast * 16.0f;
    const float invCrossSat = 1.0f / crossSaturation;
    const float crossTalkTerm = pow(tonemappedPeak, params.ChannelCrosstalk);

    ratio = pow(abs(ratio), invCrossSat.xxx) * (float3)sign(ratio);
    ratio = lerp(ratio, float3(1.0f, 1.0f, 1.0f), crossTalkTerm.xxx);
    ratio = pow(abs(ratio), crossSaturation.xxx) * (float3)sign(ratio);

    return ratio * color;
}

[SpecializationConstant]
const int TonemappingOperator = 1;

public float3 ApplyTonemapping(in const float3 color, in uniform TonemapParams params)
{
    switch (TonemappingOperator)
    {
    case 0:
        return color; // no tonemapping
    case 1:
        return LotteTonemap(color, params);
    }
}
