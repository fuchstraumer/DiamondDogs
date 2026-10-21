implementing OceanFft;
__include OceanFftCommon;
__include OceanFftSpectra;

[[vk::binding(0, 0)]] uniform CascadeParameters CascadeParams;
[[vk::binding(0, 1)]] uniform SpectrumParameters SpectrumParams;
[[vk::binding(0, 2)]] uniform RWStructuredBuffer<half4> WaveData;
[[vk::binding(0, 3)]] uniform RWStructuredBuffer<half2> h0K; // h0
[[vk::binding(0, 4)]] uniform SamplerState normSampler;
[[vk::binding(0, 5)]] uniform Texture2D normTexture;

internal uint4 Rand4DPCG32(in int4 p)
{
    // taking a signed int then reinterpreting as unsigned gives good behavior for negatives
    uint4 v = uint4(p);

    // Linear congruential step.
    v = v * 1664525u + 1013904223u;

    // shuffle
    v.x += v.y * v.w;
    v.y += v.z * v.x;
    v.z += v.x * v.y;
    v.w += v.y * v.z;

    // xoring high bits into low makes all 32 bits pretty good
    v ^= (v >> 16u);

    // final shuffle
    v.x += v.y * v.w;
    v.y += v.z * v.x;
    v.z += v.x * v.y;
    v.w += v.y * v.z;

    return v;
}

internal float4 Rand32ToFloat(in uint4 Rand32Bits)
{
    return float4(Rand32Bits >> 8) * 5.96046447754e-08;
}

internal float4 GetRandomFloat4(in int2 coordinates, in int cascade, in int seed)
{
    int4 pcgInput = int4(coordinates.x, coordinates.y, cascade, seed);
    uint4 pcgResult = Rand4DPCG32(pcgInput);
    return Rand32ToFloat(pcgResult);
}

internal float2 GetGaussianRandomFloat2(in int2 coordinates, in int cascade, in int seed)
{
    float4 random = GetRandomFloat4(coordinates, cascade, seed);
    float2 gaussianRandom;
    float u1 = max(random.x, 1.0e-7f);
    float u2 = random.y;
    float radius = sqrt(-2.0f * log(u1));
    float theta = OceanFft_2PI * u2;
    float2 scTheta; sincos(theta, scTheta.y, scTheta.x);
    return radius * scTheta;
}

internal float CascadeContribution(in float k, in uint cascade, in uniform CascadeParameters cascadeParams)
{
    float lengthScale = cascadeParams.LengthScales[cascade];
    float cutoffHigh = cascadeParams.CutoffsHigh[cascade];
    float cutoffLow = cascadeParams.CutoffsLow[cascade];

    if (k < cutoffLow || k > cutoffHigh)
    {
        return 0.0f;
    }
    else
    {
        float total = 0.0f;
        for (uint i = 0; i < FFT_NUM_WAVE_CASCADES; ++i)
        {
            bool kInRange = (k >= cascadeParams.CutoffsLow[i] && k <= cascadeParams.CutoffsHigh[i]);
            if (kInRange)
            {
                total += 1.0f;
            }
        }
        return total;
    }
}

internal float DirectionalSpreading(
    in float thetaIn,
    in float omega,
    in float peakOmega,
    in uniform float windSpeed,
    in uniform float swellAmount,
    in uniform uint DirectionalSpreadingMode,
    in uniform float ThetaOffset,
    in uniform float Alignment,
    in uniform SamplerState normSampler,
    in uniform Texture2D normTexture)
{
    float theta = thetaIn - ThetaOffset;
    // wrap theta to [-pi, pi] range, required for donelan-banner spreading, optimal for others
    // since we can swap in approximate trig functions that are only valid in that range
    theta = theta - OceanFft_2PI * round(theta / OceanFft_2PI);
    float spread = 0.0f;
    if (DirectionalSpreadingMode == OCEAN_FFT_SPREAD_MODE_MITSUYASU)
    {
        spread = MitsuyasuDirectionalSpread(theta, omega, peakOmega, windSpeed, swellAmount);
    }
    else if (DirectionalSpreadingMode == OCEAN_FFT_SPREAD_MODE_HASSELMAN)
    {
        spread = HasselmanDirectionalSpreading(theta, omega, peakOmega, windSpeed, swellAmount);
    }
    else if (DirectionalSpreadingMode == OCEAN_FFT_SPREAD_MODE_DONELAN_BANNER)
    {
        spread = DonelanBannerDirectionalSpreading(theta, omega, peakOmega, swellAmount, normSampler, normTexture);
    }

    // now get into range
    return lerp(0.5f / OceanFft_PI, spread, Alignment);
}

internal float ShortWavesFade(in float kLength, in float fadeLength)
{
    return exp(-fadeLength * fadeLength * kLength * kLength);
}

[shader("compute")]
[numthreads(8, 8, FFT_NUM_WAVE_CASCADES)]
void SpectrumInitCS(uint3 dispatchThreadID : SV_DispatchThreadID,
                    uint3 groupThreadID : SV_GroupThreadID)
{
    // even though we partition this into workgroups, it's still important to consider coords as global
    int2 coordinates = int2(dispatchThreadID.x - FFT_SIZE / 2, dispatchThreadID.y - FFT_SIZE / 2);
    int cascade = dispatchThreadID.z;
    float dK = OceanFft_2PI / cascadeParams.LengthScales[cascade];
    const float2 k = dK * float2(coordinates);
    const float kLength = max(length(k), 1e-6f);
    const uint linearIndex = LinearizeThreadIndex(dispatchThreadID, cascadeParams.Dimensions);

    float contribution = CascadeContribution(kLength, cascade, cascadeParams);
    if (contribution > 0.0f)
    {
        const float2 randomFloats = GetGaussianRandomFloat2(coordinates, cascade, spectrumParams.NoiseSeed);
        const float theta = atan2(k.y, k.x);
        float2 omegaAnddOmegadK = DispersionRelationWithDerivative(kLength, spectrumParams.Depth);
        const float dimensionlessFetch = GetDimensionlessFetch(spectrumParams.Fetch, spectrumParams.WindSpeed);
        const float peakOmega = GetJonswapPeakOmega(dimensionlessFetch, spectrumParams.WindSpeed);
        float directionalComponent = DirectionalSpreading(theta,
                                                          omegaAnddOmegadK.x,
                                                          peakOmega,
                                                          spectrumParams.WindSpeed,
                                                          spectrumParams.SwellAmount,
                                                          spectrumParams.DirectionalSpreadingMode,
                                                          spectrumParams.LocalWindDirection,
                                                          spectrumParams.Alignment,
                                                          normSampler,
                                                          normTexture);

        float freqComponent = OceanEnergySpectrum(spectrumParams.WindSpeed,
                                                  spectrumParams.Fetch,
                                                  omegaAnddOmegadK.x,
                                                  spectrumParams.Depth,
                                                  spectrumParams.PeakingFactor);

        const float fade = ShortWavesFade(kLength, spectrumParams.FadeLength);
        const float finalSpectrum = freqComponent * directionalComponent * fade;
        const float dK2 = dK * dK;
        const float dOmegaUnit = abs(omegaAnddOmegadK.y) / kLength;
        // fix: multiplied by 2.0f, which is the Tessendorf form instead of Horvath's improved form
        const float spectrumNorm = sqrt(finalSpectrum * dOmegaUnit * dK2);
        const float2 scaledPermutedSpectrum = contribution.xx * randomFloats * spectrumNorm.xx;
        // for the sake of debugging right now, we put finalSpectrum in .y
        WaveData[linearIndex] = half4(half(k.x),
                                      half(finalSpectrum),
                                      half(k.y),
                                      half(omegaAnddOmegadK.x));
        // doing it this way just bc of warnings about implicit conversions, lol
        h0K[linearIndex].x = half(scaledPermutedSpectrum.x);
        h0K[linearIndex].y = half(scaledPermutedSpectrum.y);
    }
    else
    {
        WaveData[linearIndex] = half4(half(k.x), 0.0h, half(k.y), 0.0h);
        h0K[linearIndex] = half2(0.0h, 0.0h);
    }
}

[shader("compute")]
[numthreads(8, 8, NUM_WAVE_CASCADES)]
void SpectrumConjugateCS(
    uint3 id: SV_DispatchThreadID,
    uniform CascadeParameters cascadeParams,
    uniform StructuredBuffer<half2> h0K,
    uniform RWStructuredBuffer<half2> h0KConjugate)
{
    const uint linearIndex = LinearizeThreadIndex(id, cascadeParams.Dimensions);
    
}