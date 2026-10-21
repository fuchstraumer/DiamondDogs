implementing OceanFft;
__include OceanFftCommon;
__include OceanFftBindings;
__include Spectra;

// the values of SpectrumParameters::DirectionalSpreadingMode
internal const static uint OCEAN_FFT_SPREAD_MODE_MITSUYASU = 0u;
internal const static uint OCEAN_FFT_SPREAD_MODE_HASSELMAN = 1u;
internal const static uint OCEAN_FFT_SPREAD_MODE_DONELAN_BANNER = 2u;

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

// the amplitude weight of this cascade at k: 0 outside its band, else 1 / sqrt(n) for the n cascades whose bands cover
// k. Each cascade draws its own phases, so their variances add, and the 1 / sqrt(n) keeps the total at the spectrum's
internal float CascadeContribution(in float k, in uint cascade)
{
    float lengthScale = CascadeParams.LengthScales[cascade];
    float cutoffHigh = CascadeParams.CutoffsHigh[cascade];
    float cutoffLow = CascadeParams.CutoffsLow[cascade];

    if (k < cutoffLow || k > cutoffHigh)
    {
        return 0.0f;
    }
    else
    {
        float total = 0.0f;
        for (uint i = 0; i < IFFT_NUM_WAVE_CASCADES; ++i)
        {
            bool kInRange = (k >= CascadeParams.CutoffsLow[i] && k <= CascadeParams.CutoffsHigh[i]);
            if (kInRange)
            {
                total += 1.0f;
            }
        }
        return rsqrt(total);
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
    in uniform float Alignment)
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
        spread = DonelanBannerDirectionalSpreading(theta, omega, peakOmega, swellAmount);
    }

    // now get into range
    return lerp(0.5f / OceanFft_PI, spread, Alignment);
}

internal float ShortWavesFade(in float kLength, in float fadeLength)
{
    return exp(-fadeLength * fadeLength * kLength * kLength);
}

[shader("compute")]
// dispatch (IFFT_SIZE / 8, IFFT_SIZE / 8, 1): one thread per wave vector of every cascade
[numthreads(8, 8, IFFT_NUM_WAVE_CASCADES)]
void SpectrumInitCS(uint3 dispatchThreadID : SV_DispatchThreadID,
                    uint3 groupThreadID : SV_GroupThreadID)
{
    // even though we partition this into workgroups, it's still important to consider coords as global
    int2 coordinates = int2(dispatchThreadID.xy) - int(IFFT_SIZE / 2);
    int cascade = dispatchThreadID.z;
    float dK = OceanFft_2PI / CascadeParams.LengthScales[cascade];
    const float2 k = dK * float2(coordinates);
    const float kLength = max(length(k), 1e-6f);
    const uint linearIndex = LinearizeThreadIndex(dispatchThreadID, CascadeParams.Dimensions);

    float contribution = CascadeContribution(kLength, cascade);
    if (contribution > 0.0f)
    {
        const float2 randomFloats = GetGaussianRandomFloat2(coordinates, cascade, SpectrumParams.NoiseSeed);
        const float theta = atan2(k.y, k.x);
        float2 omegaAnddOmegadK = DispersionRelationWithDerivative(kLength, SpectrumParams.Depth);
        const float dimensionlessFetch = GetDimensionlessFetch(SpectrumParams.Fetch, SpectrumParams.WindSpeed);
        const float peakOmega = GetJonswapPeakOmega(dimensionlessFetch, SpectrumParams.WindSpeed);
        float directionalComponent = DirectionalSpreading(theta,
                                                          omegaAnddOmegadK.x,
                                                          peakOmega,
                                                          SpectrumParams.WindSpeed,
                                                          SpectrumParams.SwellAmount,
                                                          SpectrumParams.DirectionalSpreadingMode,
                                                          SpectrumParams.LocalWindDirection,
                                                          SpectrumParams.Alignment);

        float freqComponent = OceanEnergySpectrum(SpectrumParams.WindSpeed,
                                                  SpectrumParams.Fetch,
                                                  omegaAnddOmegadK.x,
                                                  SpectrumParams.Depth,
                                                  SpectrumParams.PeakingFactor);

        const float fade = ShortWavesFade(kLength, SpectrumParams.FadeLength);
        float finalSpectrum = freqComponent * directionalComponent * fade;

        // the swell: long-period energy from distant wind, from its own direction. Its spreading uses the same model,
        // with full alignment: SwellSpread narrows the lobe through the same swell shaping term
        if (SpectrumParams.SwellScale > 0.0f)
        {
            const float swellFetch = GetDimensionlessFetch(SpectrumParams.SwellFetch, SpectrumParams.SwellWindSpeed);
            const float swellPeakOmega = GetJonswapPeakOmega(swellFetch, SpectrumParams.SwellWindSpeed);
            const float swellDirectional = DirectionalSpreading(theta,
                                                                omegaAnddOmegadK.x,
                                                                swellPeakOmega,
                                                                SpectrumParams.SwellWindSpeed,
                                                                SpectrumParams.SwellSpread,
                                                                SpectrumParams.DirectionalSpreadingMode,
                                                                SpectrumParams.SwellDirection,
                                                                1.0f);

            const float swellFrequency = OceanEnergySpectrum(SpectrumParams.SwellWindSpeed,
                                                             SpectrumParams.SwellFetch,
                                                             omegaAnddOmegadK.x,
                                                             SpectrumParams.Depth,
                                                             SpectrumParams.SwellPeakingFactor);

            finalSpectrum += SpectrumParams.SwellScale * swellFrequency * swellDirectional * ShortWavesFade(kLength, SpectrumParams.SwellFadeLength);
        }

        const float dK2 = dK * dK;
        const float dOmegaUnit = abs(omegaAnddOmegadK.y) / kLength;
        // fix: multiplied by 2.0f, which is the Tessendorf form instead of Horvath's improved form.
        // the field variance must be S(k) dK^2 per wave vector, and the tick's h0(k) + conj(h0(-k)) sum doubles it, so
        // E|h0|^2 = S dK^2 / 2. randomFloats is one complex Gaussian with E|xi|^2 = 2, which leaves 1/4 inside the root
        // (OceanFftTest checks the resulting significant wave height). WaveHeightScale is the artist's multiplier on top
        const float spectrumNorm = SpectrumParams.WaveHeightScale * sqrt(0.25f * finalSpectrum * dOmegaUnit * dK2);
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

// conj(h0(-k)), so the per-frame tick reads two values instead of mirroring the index itself.
// Index x holds k.x = (x - IFFT_SIZE / 2) * dK, so -k.x is at (IFFT_SIZE - x) % IFFT_SIZE: the Nyquist row maps to itself
[shader("compute")]
[numthreads(8, 8, IFFT_NUM_WAVE_CASCADES)]
void SpectrumConjugateCS(uint3 id : SV_DispatchThreadID)
{
    const uint linearIndex = LinearizeThreadIndex(id, CascadeParams.Dimensions);
    const uint3 mirrored = uint3((IFFT_SIZE - id.xy) % IFFT_SIZE, id.z);
    const half2 h0Mirrored = h0K[LinearizeThreadIndex(mirrored, CascadeParams.Dimensions)];
    h0KConjugate[linearIndex] = ComplexConj(h0Mirrored);
}
