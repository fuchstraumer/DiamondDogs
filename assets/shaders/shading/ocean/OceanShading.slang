implementing OceanRender;

// PLACEHOLDER shading, only so the demo shows something: the author is writing the real shading model. Replace
// OceanSurfaceFS freely; the vertex stage only promises OceanSurfaceVaryings and the bindings in OceanRenderCommon.

internal struct SurfaceSample
{
    float3 Normal;
    // 0 where the surface is calm, towards 1 where the horizontal displacement compresses or folds it
    float Foam;
};

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
float EvaluateWhitecapCoverage(float2 surface_xz, float distance)
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
            // k_i = J - 1 + 1/n, where J is Displacement.w. The foam weight scales its deviation from the rest value
            // 1/n, and the variance of k_i with it squared
            const float rest = 1.0f / float(OCEAN_CASCADE_COUNT);
            const float jacobianDeviation = Displacement.Sample(OceanSampler, uvw).w - 1.0f;
            const float kMean = jacobianDeviation + rest;
            const float secondMoment = Whitecaps.Sample(OceanSampler, uvw);
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
    const float epsilon = threshold * 0.4f - 0.2f;

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

// The foam that OceanFoamSimCS kept from earlier crests. Cascades combine as 1 - prod(1 - w_i f_i): a sum overshoots
// where their foam overlaps, and a max lets the coarse cascades hide the fine ones
float EvaluatePersistentFoam(float2 surface_xz)
{
    float clear = 1.0f;

    [unroll]
    for (int cascade = 0; cascade < OCEAN_CASCADE_COUNT; ++cascade)
    {
        if (ViewCascadeWeight(cascade) > 0.0f)
        {
            const float2 uv = CascadeUv(surface_xz, cascade) - FoamDriftOffset(cascade);
            const float foam = Foam.Sample(OceanSampler, float3(uv, float(cascade)));
            clear *= 1.0f - saturate(ViewFoamWeight(cascade) * foam);
        }
    }

    return (1.0f - clear) * max(0.0f, View.PersistentFoamParameters.y);
}

internal SurfaceSample SampleSurface(float2 surface_xz)
{
    float2 totalSlope = float2(0.0f);
    float2 totalStretch = float2(0.0f);
    const float distance = length(surface_xz - View.CameraPositionTime.xz);
    [unroll]
    for (uint cascade = 0; cascade < OCEAN_CASCADE_COUNT; ++cascade)
    {
        // the same distance fade as the vertex stage, so a cascade's slopes go with its displacement
        const float weight = CascadeDistanceWeight(cascade, distance);
        if (weight > 0.0f)
        {
            const float3 uvw = float3(CascadeUv(surface_xz, cascade), float(cascade));
            const float4 derivatives = Derivatives.Sample(OceanSampler, uvw) * weight;
            const float4 displacement = Displacement.Sample(OceanSampler, uvw);

            totalSlope += CascadeToWorld(derivatives.xy, cascade);
            totalStretch += CascadeStretchToWorld(derivatives.zw, cascade);

            // Jacobian in displacement.w: calm water is 1.0, crest compression is < 1.0, folding occurs below ~0.3.
            // Only highlight breaking crests where surface is folded AND height is positive.
            // Cascade 0 (swell) is a long roller that rarely whitecaps on its own; cascades 1-4 provide steep chop.
            const float whitecapThreshold = cascade == 0 ? 0.20f : 0.50f;
            const float fold = max(0.0f, whitecapThreshold - displacement.w);
            const float crest = saturate(displacement.y * 1.5f + 0.1f);
        }
    }
    SurfaceSample result;
    // Clamping the denominator prevents division by zero and normal spikes when waves fold
    const float2 slope = totalSlope / max(float2(0.1f), 1.0f + totalStretch);
    result.Normal = normalize(float3(-slope.x, 1.0f, -slope.y));
    return result;
}

[shader("fragment")]
float4 OceanSurfaceFS(OceanSurfaceVaryings input) : SV_Target
{
    const SurfaceSample surface = SampleSurface(input.SurfaceXZ);
    const float3 toCamera = normalize(View.CameraPositionTime.xyz - input.WorldPosition);
    const float3 toSun = View.SunDirectionSeabedDepth.xyz;

    const float3 deepColor = float3(0.004f, 0.016f, 0.035f);
    const float3 skyColor = lerp(float3(0.35f, 0.5f, 0.7f), float3(0.08f, 0.2f, 0.45f), saturate(reflect(-toCamera, surface.Normal).y));
    const float fresnel = 0.02f + 0.98f * pow(1.0f - saturate(dot(surface.Normal, toCamera)), 5.0f);
    const float3 halfVector = normalize(toSun + toCamera);
    const float sun = pow(saturate(dot(surface.Normal, halfVector)), 400.0f) * 12.0f;

    // evaluate whitecap coverage based on the precomputed moments
    const float distance = length(input.SurfaceXZ - View.CameraPositionTime.xz);
    const float breaking = EvaluateWhitecapCoverage(input.SurfaceXZ, distance);
    // the persistent foam only fills in behind the crests: max keeps the breaking crest as it was
    const float persistent = View.PersistentFoamParameters.x > 0.5f ? EvaluatePersistentFoam(input.SurfaceXZ) : 0.0f;
    const float whitecaps = saturate(max(breaking, persistent));
    // evaluated as classic lambertian diffuse with albedo 0.40
    const float nDotL = saturate(dot(surface.Normal, toSun));
    const float3 foamAlbedo = 0.85f.xxx;
    const float3 foamRadiance = foamAlbedo * (nDotL * float3(1.0f, 0.95f, 0.9f) + skyColor * 0.5f);

    const float3 waterBase = lerp(deepColor, skyColor, fresnel);
    float3 color = lerp(waterBase, foamRadiance, whitecaps) + sun.xxx;

    // scale hdr to match my monitor paper white
    // k_PaperWhiteNits and k_ScRgbRefNits must stay equal to the ones in OceanScene.cpp, which scales captures back
    static const float k_PaperWhiteNits = 348.0f;
    static const float k_PeakNits = 600.0f;
    static const float k_ScRgbRefNits = 80.0f;
    color *= (k_PaperWhiteNits / k_ScRgbRefNits);
    // smoothly roll off highlights beyond the peak nits to prevent clipping
    const float k_peakScRgb = k_PeakNits / k_ScRgbRefNits;
    color = min(color, float3(k_peakScRgb));
    return float4(color, 0.96f);
}
