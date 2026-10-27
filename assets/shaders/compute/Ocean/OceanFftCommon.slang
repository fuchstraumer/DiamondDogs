implementing OceanFft;
__include OceanFftDims;
__include OceanFftBindings;

internal const static half IFFT_RADIX2_SCALE_FACTOR = 0.5h;
internal const static half IFFT_RADIX4_SCALE_FACTOR = 0.25h;
internal const static float OceanFft_PI = 3.1415926535897932384626433832795f;
internal const static float OceanFft_2PI = 6.283185307179586476925286766559f;
internal const static float OceanFft_G = 9.81f;
internal const static float OceanFft_InvG = 1.0f / OceanFft_G;
internal const static float OceanFft_SigmaOverRho = 0.074e-3f; // surface tension over density of water

public struct SpectrumParameters
{
    uint NoiseSeed;
    uint DirectionalSpreadingMode;
    float WindSpeed;
    float Fetch;
    float Depth;
    float SwellAmount;
    float Alignment;
    float LocalWindDirection;
    float SwellDirection;
    float FadeLength;
    float PeakingFactor;
    // multiplies every amplitude: 1 is the physical spectrum (OceanFftTest checks its wave height)
    float WaveHeightScale;
    // the swell: a second JONSWAP spectrum from distant wind, added to the local wind sea's energy. It has its own
    // direction (SwellDirection, above), and SwellSpread narrows its spreading lobe. SwellScale 0 turns it off
    float SwellScale;
    float SwellWindSpeed;
    float SwellFetch;
    float SwellPeakingFactor;
    float SwellSpread;
    float SwellFadeLength;
};

// each per-cascade array holds eight cascades in two float4s. Read them through the helpers below
public struct CascadeParameters
{
    float4 LengthScales[2];
    float4 CutoffsHigh[2];
    float4 CutoffsLow[2];
    uint2 Dimensions;
    float2 Pad0;
};

internal float CascadeLengthScale(uint cascade)
{
    return CascadeParams.LengthScales[cascade >> 2u][cascade & 3u];
}

internal float CascadeCutoffHigh(uint cascade)
{
    return CascadeParams.CutoffsHigh[cascade >> 2u][cascade & 3u];
}

internal float CascadeCutoffLow(uint cascade)
{
    return CascadeParams.CutoffsLow[cascade >> 2u][cascade & 3u];
}

internal float WaveSpacing(uint cascade)
{
    return OceanFft_2PI / CascadeLengthScale(cascade);
}

public struct IfftParameters
{
    bool Transpose;
    uint WaveSize;
    uint Log4WaveSize;
    uint Pad0;
}

internal int LinearizeThreadIndex(uint3 threadIdx, in uniform uint2 dimensions)
{
    return threadIdx.x + (dimensions.x * (threadIdx.y + (dimensions.y * threadIdx.z)));
}

internal float2 ComplexAdd(in float2 a, in float2 b)
{
    return a + b;
}

internal half2 ComplexAdd(in half2 a, in half2 b)
{
    return a + b;
}

internal float2 ComplexSub(in float2 a, in float2 b)
{
    return a - b;
}

internal half2 ComplexSub(in half2 a, in half2 b)
{
    return a - b;
}

internal float2 ComplexExp(in float x)
{
    float2 result;
    sincos(x, result.y, result.x);
    return result;
}

internal half2 ComplexExp(in half x)
{
    half2 result;
    sincos(x, result.y, result.x);
    return result;
}

// LUT-based complex exponential, allowing us to stay entirely in half precision pipe for IDFT math
// RGA shows F32 ops showing up only around this function, so this should help knock those into f16.
// The table holds e^(-i 2 pi n / OCEAN_FFT_SINCOS_LUT_SIZE) (y pre-conjugated, so one less mul), built for the largest
// IFFT size. This size reads every (OCEAN_FFT_SINCOS_LUT_SIZE / IFFT_SIZE)th entry with an integer load, so no two
// entries are ever blended. A caller that needs e^(+i theta) negates .y
internal half2 ComplexExp(in const uint log2GroupSize, in const uint j, in const uint tapIndex)
{
    const uint mask = (1u << log2GroupSize) - 1u;
    const uint p = (j * tapIndex & mask) << (IFFT_LOG2_SIZE - log2GroupSize);
    return IfftSinCosLUT.Load(int2(int(p << (OCEAN_FFT_SINCOS_LUT_LOG2_SIZE - IFFT_LOG2_SIZE)), 0));
}

internal float2 ComplexMul(in float2 a, in float2 b)
{
    // (a.x*b.x - a.y*b.y, a.x*b.y + a.y*b.x)
    float2 addend = a.yy * b.yx;
    return mad(a.xx, b, float2(-addend.x, addend.y));
}

internal half2 ComplexMul(in half2 a, in half2 b)
{
    // mad() not defined for halves, do it old fashioned way
    return half2(a.x * b.x - a.y * b.y, a.x * b.y + a.y * b.x);
}

internal float2 ComplexConj(in float2 a)
{
    return float2(a.x, -a.y);
}

internal half2 ComplexConj(in half2 a)
{
    return half2(a.x, -a.y);
}

internal float2 ComplexMulI(in float2 a)
{
    return float2(-a.y, a.x);
}

internal half2 ComplexMulI(in half2 a)
{
    return half2(-a.y, a.x);
}

internal float2 ComplexMulNegI(in float2 a)
{
    return float2(a.y, -a.x);
}

internal half2 ComplexMulNegI(in half2 a)
{
    return half2(a.y, -a.x);
}

internal float2 ComplexDiv(in float2 a, in float2 b)
{
    float2 denom = 1.0f / dot(a, b);
    return ComplexMul(a, ComplexConj(b)) * denom.xx;
}

internal half2 ComplexDiv(in half2 a, in half2 b)
{
    half2 denom = 1.0h / dot(a, b);
    return ComplexMul(a, ComplexConj(b)) * denom.xx;
}

