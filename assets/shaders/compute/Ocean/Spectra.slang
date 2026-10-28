implementing OceanFft;
__include OceanFftCommon;
// the Q(s) polynomial fits and SampleDonelanBannerLUT, generated from assets/scripts/OceanFftConfig.yaml by
// ocean_fft_support.py as a build step
__include OceanFftGenerated;

internal float GetDimensionlessFetch(in float fetch, in float windSpeed)
{
    // 1000.0f = 1km in meters, and we divide by windSpeed^2 to get a dimensionless number
    return min(abs((fetch * 1000.0f * OceanFft_G) / (windSpeed * windSpeed)), 1e6f);
}

internal float GetJonswapAlpha(in float dimensionlessFetch)
{
    return 0.076f * pow(dimensionlessFetch, -0.22f);
}

internal float GetJonswapPeakOmega(in float dimensionlessFetch, in float windSpeed)
{
    return OceanFft_2PI * 3.50f * abs(OceanFft_G / windSpeed) * pow(dimensionlessFetch, -0.33f);
}

// peak sharpening is gamma raised to this value
internal float GetPeakSharpeningPower(in float omega, in float peakOmega)
{
    float sigma = (omega <= peakOmega) ? 2.0f * (0.07f * 0.07f) : 2.0f * (0.09f * 0.09f);
    const float numerator = (omega - peakOmega) * (omega - peakOmega);
    const float denominator = sigma * (peakOmega * peakOmega);
    return exp(-(numerator / denominator));
}

internal float KitaiGorodskiiDepthCorrection(in float omega, in float depth)
{
    // graphed smoothstep vs the piecewise, and despite the Horvath paper suggesting that 0-2.2 is the right interval, we actaully get
    // a tighter fit with 0-2.0. I think the piecewise is still the tightest fit we could get without implementing the full correction
    // (which isn't happening), but this is a good compromise and is nice and branchless and still clamped
    // bizarrely, horvath's own tanh approximation is actually a worse fit than all of the alternatives. i think he chose it for boundary
    // condition handling happening gracefully
    const float omegaH = clamp(omega * sqrt(depth * OceanFft_InvG), 0.0f, 2.0f);
    const float low = 0.5f * omegaH * omegaH;
    const float high = 1.0f - 0.5f * (2.0f - omegaH) * (2.0f - omegaH);
    return (omegaH <= 1.0f) ? low : high;
}

// all ocean spectra are some form of alpha-beta spectrum, so I'm implementing this here to try and shove it into my brain
internal float AlphaBetaSpectrum(in float A, in float B, in float omega, in float peakOmega)
{
    const float omegaTerm = omega * omega * omega * omega * omega;
    const float alphaTerm = (A * (OceanFft_G * OceanFft_G)) / omegaTerm;
    const float peakOmegaOverOmega = peakOmega / omega;
    const float peakOmegaTerm = peakOmegaOverOmega * peakOmegaOverOmega * peakOmegaOverOmega * peakOmegaOverOmega;
    const float betaTerm = exp(-B * peakOmegaTerm);
    return alphaTerm * betaTerm;
}

/**
windSpeed: wind speed at 10m in m/s
fetch: distance over which the wind blows in km (km!)
omega:
depth: depth of the water in meters
gamma: peak sharpening factor, default 3.3 (for now)

returns the energy spectrum, based on Jonswap with depth correction.
needs to be multiplied by the directional spectrum component
*/
internal float OceanEnergySpectrum(in float windSpeed, in float fetch, in float omega, in float depth, in float gamma)
{
    const float dimensionlessFetch = min(GetDimensionlessFetch(fetch, windSpeed), 1e6f);
    const float alpha = GetJonswapAlpha(dimensionlessFetch);
    const float peakOmega = GetJonswapPeakOmega(dimensionlessFetch, windSpeed);
    const float peakSharpeningPower = GetPeakSharpeningPower(omega, peakOmega);
    const float peakSharpening = pow(gamma, peakSharpeningPower);
    static const float jonswapBeta = 1.25f;
    const float baseSpectrum = AlphaBetaSpectrum(alpha, jonswapBeta, omega, peakOmega);
    const float correction = KitaiGorodskiiDepthCorrection(omega, depth);
    return peakSharpening * (correction * baseSpectrum);
}

// When depth is much much greater than wavelength, dispersion relation is w^2 = g*k.
// When depth is much much less than wavelength, dispersion relation is w^2 = gktanh(kh).
// For capillary waves, the dispersion relation is w^2 = gk + (T/rho)k^3, where T is surface tension
// and rho is water density.
// Like the reference paper, we use the capillary formation as k is inversely proportional to wavelength,
// so for small wavelengths (high k) the capillary term dominates: for swells, it is negligible.
// Dispersion is our spatial frequency (w) as a function of wave number (k) and depth (h)
internal float DispersionRelation(in float kLength, in float depth)
{
    const float capillaryTerm = OceanFft_SigmaOverRho * kLength * kLength * kLength;
    const float gravityTerm = OceanFft_G * kLength;
    const float depthTerm = tanh(min(kLength * depth, 10.0f));
    return sqrt((gravityTerm + capillaryTerm) * depthTerm);
}

// Calculates the dispersion relation and it's derivative with respect to k, in one operation
// since we only ever use these together (during initial spectrum computation)
internal float2 DispersionRelationWithDerivative(in float kLength, in float depth)
{
    float2 result = float2(0.0f, 0.0f);
    const float capillaryTerm = OceanFft_SigmaOverRho * kLength * kLength;
    const float capillaryPlusGravityTerm = OceanFft_G + capillaryTerm;
    const float depthTerm = tanh(min(kLength * depth, 10.0f));
    result.x = sqrt(abs(kLength * capillaryPlusGravityTerm * depthTerm));
    // now get derivative. based on horvath paper and his notes
    // pre-invert so we can multiply instead of divide
    const float coshTerm = cosh(min(kLength * depth, 10.0f));
    const float sechTerm = 1.0f / (coshTerm * coshTerm);
    const float numeratorPart1 = (capillaryPlusGravityTerm + capillaryTerm + capillaryTerm) * depthTerm;
    const float numeratorPart2 = (min(kLength * depth, 10.0f) * capillaryPlusGravityTerm) * sechTerm;
    result.y = abs(numeratorPart1 + numeratorPart2) / (2.0f * result.x);
    return result;
}

internal float DonelanBannerBeta(in float omega, in float peakOmega)
{
    // Watching occupancy and VGPRs get bigger the more of this we implement.... sad
    const float omegaOverPeakOmega = omega / peakOmega;
    if (omegaOverPeakOmega < 0.56f)
    {
        return 1.24f;
    }
    else if (omegaOverPeakOmega < 0.95f)
    {
        return 2.61f * pow(omegaOverPeakOmega, 1.3f);
    }
    else if (omegaOverPeakOmega < 1.60f)
    {
        return 2.28f * pow(omegaOverPeakOmega, -1.3f);
    }
    else
    {
        const float exponent = -0.4f + 0.8393f * exp(-0.567f * log(omegaOverPeakOmega * omegaOverPeakOmega));
        return pow(10.0f, exponent);
    }
}

internal float SwellShaper(in float omega, in float peakOmega, in float swellAmount)
{
    const float peakOmegaOverOmega = peakOmega / omega;
    // Cap the shaping exponent so discrete Fourier bins at low wavenumbers retain angular width,
    // creating natural crest length variation instead of infinite parallel grating lines.
    return min(16.1f * tanh(peakOmegaOverOmega) * (swellAmount * swellAmount), 16.0f);
}

internal float SwellDirectionalSpreading(in float theta, in float omega, in float peakOmega, in float swellAmount)
{
    const float swell = 2.0f * SwellShaper(omega, peakOmega, swellAmount);
    const float spread = abs(cos(theta * 0.5f));
    return pow(spread, swell);
}

internal float SpreadPowerMitsuyasu(in float omega, in float peakOmega, in float windSpeed)
{
    const float x = (peakOmega * windSpeed) * OceanFft_InvG;
    const float xPow = pow(x, -2.5f);
    return 11.5f * xPow;
}

internal float SpreadFactorMitsuyasu(in float spreadPower, in float omega, in float peakOmega)
{
    const float exponent = omega <= peakOmega ? 5.0f : -2.5f;
    return spreadPower * pow(omega / peakOmega, exponent);
}

internal float SpreadPowerHasselman(in float omega, in float peakOmega, in float windSpeed)
{
    if (omega <= peakOmega)
    {
        return 4.06f;
    }
    else
    {
        const float inputFactor = (windSpeed * peakOmega) * OceanFft_InvG;
        return -2.33f - (1.45f * (inputFactor - 1.17f));
    }
}

internal float SpreadFactorHasselman(in float spreadPower, in float omega, in float peakOmega)
{
    const float factor = omega <= peakOmega ? 6.97f : 9.77f;
    return factor * pow(omega / peakOmega, spreadPower);
}

internal float PositiveCosineSquaredDirectionalSpreading(in float theta, in float spreadPower)
{
    const float cosTheta = abs(cos(theta * 0.5f));
    return pow(cosTheta, 2.0f * spreadPower);
}

internal float MitsuyasuDirectionalSpread(
    in float theta,
    in float omega,
    in float peakOmega,
    in float windSpeed,
    in float swellAmount)
{
    const float spreadPower = SpreadPowerMitsuyasu(omega, peakOmega, windSpeed);
    // fix: we should use the spread factor for normalization, not the raw spread power
    const float spreadFactor = SpreadFactorMitsuyasu(spreadPower, omega, peakOmega);
    // fix: swell amount from SwellShaper (not other fn) is *added* to shapeFactor 
    const float finalSpreadFactor = spreadFactor + SwellShaper(omega, peakOmega, swellAmount);
    const float norm = MitsuyasuNormalizationFactor(finalSpreadFactor);
    return PositiveCosineSquaredDirectionalSpreading(theta, finalSpreadFactor) * norm;
}

internal float HasselmanDirectionalSpreading(
    in float theta,
    in float omega,
    in float peakOmega,
    in float windSpeed,
    in float swellAmount)
{
    const float spreadPower = SpreadPowerHasselman(omega, peakOmega, windSpeed);
    const float spreadFactor = SpreadFactorHasselman(spreadPower, omega, peakOmega);
    // fix: same issue as mitsuyasu. we just add SwellShaper to the spread *factor*
    const float finalSpreadFactor = spreadFactor + SwellShaper(omega, peakOmega, swellAmount);
    const float norm = HasselmanNormalizationFactor(finalSpreadFactor);
    return PositiveCosineSquaredDirectionalSpreading(theta, finalSpreadFactor) * norm;
}

// closed form of Donelan-Banner directional spreading. d/dTheta[tanh(beta * theta)] = beta * sech^2(beta * theta)
// for our normalization, we need to integrate this over the unit circle and divide by the integral to normalize it.
// integral goes from -pi to pi (the unit circle), so using fundamental theorem of calculus we know we can just do
// 1 / beta * (tanh(beta * pi) - tanh(-beta * pi)) = 2 / beta * tanh(beta * pi)
// (because tanh is odd, so tanh(-x) = -tanh(x))
// neat!
internal float DonelanBannerDirectionalSpreading(
    in float theta,
    in float omega,
    in float peakOmega,
    in float swellAmount)
{
    const float beta = DonelanBannerBeta(omega, peakOmega);               // u axis
    const float swellShape = SwellShaper(omega, peakOmega, swellAmount);  // v axis
    // LUT is stored as the inverse, so we can just mul it
    const float norm = SampleDonelanBannerLUT(beta, swellShape);
    const float db = 1.0f / cosh(theta * beta);
    const float db2 = db * db;
    const float swell = SwellDirectionalSpreading(theta, omega, peakOmega, swellAmount);
    return (db2 * swell) * norm;
}