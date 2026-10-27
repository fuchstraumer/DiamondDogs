implementing OceanRender;

// Whitecap coverage, shared by the fragment stage (OceanShading.slang) and the persistent foam (OceanFoam.slang), so
// that every foam slider drives both

float FastErf(float x)
{
    static const float a1 = 0.254829592f;
    static const float a2 = -0.284496736f;
    static const float a3 = 1.421413741f;
    static const float a4 = -1.453152027f;
    static const float a5 = 1.061405429f;
    static const float p = 0.3275911f;
    float signX = sign(x);
    float absX = abs(x);
    const float t = 1.0f / (1.0f + p * absX);
    const float y = 1.0f - (((((a5 * t + a4) * t) + a3) * t + a2) * t + a1) * t * exp(-absX * absX);
    return signX * y;
}

// See Dupuy et al. 2012, Real-time Animation and Rendering of Ocean Whitecaps
// grad_x and grad_y are the footprint in world xz: ddx and ddy of the surface position in the fragment stage, one
// texel in a compute pass. CascadeUv is affine, so mapping them into each cascade gives the same filter as an implicit
// Sample. threshold_bias raises the breaking threshold (in Jacobian units): the persistent foam passes its value here
float EvaluateWhitecapCoverage(float2 surface_xz, float2 grad_x, float2 grad_y, float threshold_bias)
{
    float muA = 0.0f;
    float varA = 0.0f;

    [unroll]
    for (int cascade = 0; cascade < OCEAN_CASCADE_COUNT; ++cascade)
    {
        const float enabled = ViewCascadeWeight(cascade);
        if (enabled > 0.0f)
        {
            const float3 uvw = float3(CascadeUv(surface_xz, cascade), float(cascade));
            const float2 uvGradX = WorldToCascade(grad_x, cascade) / ViewLengthScale(cascade);
            const float2 uvGradY = WorldToCascade(grad_y, cascade) / ViewLengthScale(cascade);
            // k_i = J - 1 + 1/n, where J is Displacement.w. The foam weight scales its deviation from the rest value
            // 1/n, and the variance of k_i with it squared
            const float rest = 1.0f / float(OCEAN_CASCADE_COUNT);
            const float jacobianDeviation = Displacement.SampleGrad(OceanSampler, uvw, uvGradX, uvGradY).w - 1.0f;
            const float kMean = jacobianDeviation + rest;
            const float secondMoment = Whitecaps.SampleGrad(OceanSampler, uvw, uvGradX, uvGradY);
            const float foamWeight = ViewFoamWeight(cascade);
            muA += rest + foamWeight * jacobianDeviation;
            varA += foamWeight * foamWeight * max(0.0f, secondMoment - kMean * kMean);
        }
        else
        {
            // if cascade disabled, add "rest state" value with no variance
            muA += 1.0f / float(OCEAN_CASCADE_COUNT);
        }
    }

    // FoamSpread: scale footprint dispersion (>1 softer/broader, <1 tighter crest lines)
    // Hard clamp to prevent division by zero or negative dispersion
    const float foamSpread = max(0.05f, View.WhitecapParameters.z);
    const float sigmaSq = max(1e-4f, varA);
    const float sigmaA = sqrt(sigmaSq) * foamSpread;

    // Wave breaking threshold: [-0.2, 0.2] default range
    // Soft range in UI [0, 1]; hard clamp allows creative pushing while stopping blowout
    const float threshold = clamp(View.WhitecapParameters.x, -0.5f, 1.5f);
    const float epsilon = threshold * 0.4f - 0.2f + threshold_bias;

    // Closed-form Gaussian integral over the pixel footprint (0.70710678 = sqrt(2)/2)
    const float errfIn = 0.70710678f * (epsilon - muA) / sigmaA;
    const float rawCoverage = saturate(0.5f + (0.5f * FastErf(errfIn)));

    // FoamContrast: gamma falloff curve (>1 punchy cores, <1 broad milky froth)
    // Hard clamp to avoid zero/negative exponents in pow()
    const float foamContrast = clamp(View.WhitecapParameters.w, 0.05f, 8.0f);
    const float shapedCoverage = pow(rawCoverage, foamContrast);

    // FoamAmount: linear coverage multiplier (0 = none, 1 = physical Dupuy, 2+ = heavy foam)
    // Hard clamp against negative amounts; saturate clamps maximum pixel coverage to 1.0
    const float foamAmount = max(0.0f, View.WhitecapParameters.y);
    const float whitecapCoverage = saturate(shapedCoverage * foamAmount);

    return whitecapCoverage;
}
