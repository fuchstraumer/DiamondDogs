implementing OceanRender;

// Ocean surface shading module.
// Implements the Ross-Bruneton-LEADR anisotropic specular BRDF,
// Bruneton effective Fresnel reflection, LEADR-biased sky reflections,
// AP1 forward crest subsurface scattering, and Portsmouth et al. (2024) EON foam shading.

internal struct SurfaceSample
{
    float3 Normal;
    float2 SigmaSq;
    float DisplacementY;
    float2 RawSlope;
    float2 TotalStretch;
    float2 FwidthVariance;
    float JacobianMuA;
    float FinestMipLevel;
    float3 CascadeWeightRgb;
    // for the fine_energy debug view: the slope variance of the two finest cascades, drawn (in the normal) and hidden
    // (LEAN roughness, which depends on the pixel footprint), and the horizontal stretch of the coarse cascades alone,
    // which places the pixel on the long wave (crest or trough)
    float FineDrawnEnergy;
    float FineHiddenEnergy;
    float2 CoarseStretch;
};

internal void SampleSurfaceAndWhitecaps(
    float2 surface_xz,
    float2 ddxXz,
    float2 ddyXz,
    float wave_height,
    float threshold_bias,
    out SurfaceSample surface,
    out float whitecaps)
{
    float2 totalSlope = float2(0.0f);
    float2 totalStretch = float2(0.0f);
    float2 unresolvedVariance = float2(0.0f);
    float fineDrawnEnergy = 0.0f;
    float fineHiddenEnergy = 0.0f;
    float2 coarseStretch = float2(0.0f);
    const float distance = length(surface_xz - View.CameraPositionTime.xz);

    float muA = 0.0f;
    float varA = 0.0f;
    float3 tensor = float3(0.0f);
    const float rest = 1.0f / float(OCEAN_CASCADE_COUNT);
    float finestMipLevel = 0.0f;

    [unroll]
    for (uint cascade = 0; cascade < OCEAN_CASCADE_COUNT; ++cascade)
    {
        const float enabled = ViewCascadeWeight(cascade);
        // enabled is the A/B weight alone. weight adds the distance fade. SampleGrad and the mips already filter each
        // cascade to the pixel footprint, so no second footprint fade is applied here
        const float weight = CascadeDistanceWeight(cascade, distance);
        const float foamWeight = ViewFoamWeight(cascade);

        const float2 ddxUv = WorldToCascade(ddxXz, cascade) / ViewLengthScale(cascade);
        const float2 ddyUv = WorldToCascade(ddyXz, cascade) / ViewLengthScale(cascade);
        const float2 ddxTexel = ddxUv * View.LodParameters.y;
        const float2 ddyTexel = ddyUv * View.LodParameters.y;
        const float rho = max(length(ddxTexel), length(ddyTexel));
        if (cascade == OCEAN_CASCADE_COUNT - 1u)
        {
            finestMipLevel = max(0.0f, log2(max(1e-6f, rho)));
        }

        if (enabled > 0.0f || foamWeight > 0.0f)
        {
            const float3 uvw = float3(CascadeUv(surface_xz, cascade), float(cascade));
            const float4 derivatives = Derivatives.SampleGrad(OceanSampler, uvw, ddxUv, ddyUv);
            const float4 moments = Whitecaps.SampleGrad(OceanSampler, uvw, ddxUv, ddyUv);

            if (enabled > 0.0f)
            {
                // the mips already average the slope over the footprint, so the normal takes it as sampled
                const float4 scaledDeriv = derivatives * weight;
                const float2 cascadeSlope = CascadeToWorld(scaledDeriv.xy, cascade);
                const float2 cascadeStretch = CascadeStretchToWorld(scaledDeriv.zw, cascade);
                totalSlope += cascadeSlope;
                totalStretch += cascadeStretch;

                if (cascade + 2u >= OCEAN_CASCADE_COUNT)
                {
                    fineDrawnEnergy += dot(cascadeSlope, cascadeSlope);
                }
                else
                {
                    coarseStretch += cascadeStretch;
                }

                // LEAN mapping (Olano and Baker 2010): the mean of the squared slope minus the square of the mean slope
                // is the variance that the footprint hides. The slope that the distance fade takes out of the normal
                // is added to it, so a faded cascade becomes roughness and its energy stays in the highlight.
                // The second moment of (enabled * s - weight * mean) is enabled^2 var + (enabled - weight)^2 mean^2
                const float2 meanSlope = derivatives.xy;
                const float2 hiddenVariance = max(float2(0.0f), moments.zw - meanSlope * meanSlope);
                const float fadedPart = enabled - weight;
                const float2 cascadeVariance = (enabled * enabled) * hiddenVariance + (fadedPart * fadedPart) * (meanSlope * meanSlope);
                // only the diagonal of the slope covariance is stored, so the rotation drops its off-diagonal term, as
                // CascadeStretchToWorld does
                const float2 cascadeHiddenVariance = CascadeStretchToWorld(cascadeVariance, cascade);
                unresolvedVariance += cascadeHiddenVariance;

                if (cascade + 2u >= OCEAN_CASCADE_COUNT)
                {
                    fineHiddenEnergy += cascadeHiddenVariance.x + cascadeHiddenVariance.y;
                }
            }

            if (foamWeight > 0.0f)
            {
                const float jacobianDeviation = Displacement.SampleGrad(OceanSampler, uvw, ddxUv, ddyUv).w - 1.0f;
                const float kMean = jacobianDeviation + rest;
                const float2 stretch = derivatives.zw;

                muA += rest + foamWeight * jacobianDeviation;
                varA += foamWeight * foamWeight * max(0.0f, moments.x - kMean * kMean);
                tensor += foamWeight * CascadeTensorToWorld(float3(stretch, moments.y), cascade);
            }
            else
            {
                muA += rest;
            }
        }
        else
        {
            muA += rest;
        }
    }

    const float2 slope = totalSlope / max(float2(0.1f), 1.0f + totalStretch);
    surface.Normal = normalize(float3(-slope.x, 1.0f, -slope.y));

    const float2 dNx = ddx_fine(surface.Normal.xz);
    const float2 dNy = ddy_fine(surface.Normal.xz);
    const float2 fwidthVariance = float2(dNx.x * dNx.x + dNy.x * dNy.x, dNx.y * dNx.y + dNy.y * dNy.y);

    const float2 baseVariance = float2(0.005f, 0.005f);
    const float2 leadrVariance = unresolvedVariance * Frame.LeadrSlopeVarianceScale;
    surface.SigmaSq = max(float2(1e-4f), baseVariance + leadrVariance);
    surface.DisplacementY = wave_height;
    surface.RawSlope = totalSlope;
    surface.TotalStretch = totalStretch;
    surface.FwidthVariance = fwidthVariance;
    surface.FinestMipLevel = finestMipLevel;
    surface.CascadeWeightRgb = float3(
        CascadeDistanceWeight(2u, distance),
        CascadeDistanceWeight(3u, distance),
        CascadeDistanceWeight(4u, distance));
    surface.FineDrawnEnergy = fineDrawnEnergy;
    surface.FineHiddenEnergy = fineHiddenEnergy;
    surface.CoarseStretch = coarseStretch;

    // Crest sharpness
    const float jxx = 1.0f + tensor.x;
    const float jzz = 1.0f + tensor.y;
    const float halfDifference = 0.5f * (jxx - jzz);
    const float minorEigenvalue = 0.5f * (jxx + jzz) - sqrt(halfDifference * halfDifference + tensor.z * tensor.z);
    muA = lerp(muA, minorEigenvalue, saturate(View.WhitecapParameters.z));
    surface.JacobianMuA = muA;

    const float sigmaSq = max(1e-4f, varA);
    const float sigmaA = sqrt(sigmaSq);

    const float threshold = clamp(View.WhitecapParameters.x, -0.5f, 1.5f);
    const float epsilon = threshold * 0.4f - 0.2f + threshold_bias;
    const float errfIn = 0.70710678f * (epsilon - muA) / sigmaA;
    const float rawCoverage = saturate(0.5f + (0.5f * FastErf(errfIn)));
    const float foamAmount = max(0.0f, View.WhitecapParameters.y);
    whitecaps = saturate(rawCoverage * foamAmount);
}

internal bool IsGBufferSurfaceDebugMode(uint mode)
{
    return mode == SURF_DEBUG_SLOPE
        || mode == SURF_DEBUG_STRETCH
        || mode == SURF_DEBUG_JACOBIAN
        || mode == SURF_DEBUG_MIP_LEVEL
        || mode == SURF_DEBUG_CASCADE_WEIGHTS
        || mode == SURF_DEBUG_SLOPE_DIRECTION
        || mode == SURF_DEBUG_COMPRESSION
        || mode == SURF_DEBUG_FINE_ENERGY;
}

internal float3 HueToRgb(float hue)
{
    const float3 rgb = abs(frac(hue + float3(0.0f, 2.0f / 3.0f, 1.0f / 3.0f)) * 6.0f - 3.0f) - 1.0f;
    return saturate(rgb);
}

internal float3 EvaluateGBufferSurfaceDebug(uint mode, SurfaceSample surface)
{
    if (mode == SURF_DEBUG_SLOPE)
    {
        const float2 stretchedSlope = surface.RawSlope / max(float2(0.1f), 1.0f + surface.TotalStretch);
        const float stretchRatio = length(stretchedSlope) / max(1e-4f, length(surface.RawSlope));
        return float3(
            saturate(surface.RawSlope.x * 0.5f + 0.5f),
            saturate(surface.RawSlope.y * 0.5f + 0.5f),
            saturate((stretchRatio - 1.0f) * 0.25f));
    }
    if (mode == SURF_DEBUG_STRETCH)
    {
        const float minDenom = min(1.0f + surface.TotalStretch.x, 1.0f + surface.TotalStretch.y);
        if (minDenom <= 0.105f)
        {
            return float3(1.0f, 0.0f, 1.0f);
        }
        if (minDenom < 0.5f)
        {
            const float t = saturate((0.5f - minDenom) / 0.4f);
            return lerp(float3(1.0f, 0.8f, 0.0f), float3(1.0f, 0.05f, 0.0f), t);
        }
        const float expand = saturate((minDenom - 0.5f) / 1.5f);
        return lerp(float3(0.2f, 0.2f, 0.2f), float3(0.1f, 0.7f, 1.0f), expand);
    }
    if (mode == SURF_DEBUG_JACOBIAN)
    {
        const float j = surface.JacobianMuA;
        if (j <= 0.0f)
        {
            return float3(1.0f, 0.0f, 0.0f);
        }
        if (j < 1.0f)
        {
            return lerp(float3(1.0f, 0.75f, 0.0f), float3(0.25f, 0.25f, 0.25f), j);
        }
        return lerp(float3(0.25f, 0.25f, 0.25f), float3(0.1f, 0.5f, 1.0f), saturate((j - 1.0f) * 0.8f));
    }
    if (mode == SURF_DEBUG_MIP_LEVEL)
    {
        const float m = clamp(surface.FinestMipLevel, 0.0f, 6.0f);
        if (m < 1.0f) return lerp(float3(1.0f, 0.0f, 0.0f), float3(1.0f, 0.5f, 0.0f), m);
        if (m < 2.0f) return lerp(float3(1.0f, 0.5f, 0.0f), float3(1.0f, 1.0f, 0.0f), m - 1.0f);
        if (m < 3.0f) return lerp(float3(1.0f, 1.0f, 0.0f), float3(0.0f, 0.9f, 0.2f), m - 2.0f);
        if (m < 4.0f) return lerp(float3(0.0f, 0.9f, 0.2f), float3(0.0f, 0.8f, 1.0f), m - 3.0f);
        return lerp(float3(0.0f, 0.8f, 1.0f), float3(0.1f, 0.1f, 1.0f), saturate((m - 4.0f) * 0.5f));
    }
    if (mode == SURF_DEBUG_CASCADE_WEIGHTS)
    {
        return surface.CascadeWeightRgb;
    }
    // hue: the direction the surface slopes towards, in world space (+x red, +z about blue-violet). Brightness: the
    // steepness. Spreading models differ here: an aligned sea shows two hues, a confused sea every hue
    if (mode == SURF_DEBUG_SLOPE_DIRECTION)
    {
        const float2 slope = -surface.Normal.xz / max(1e-4f, surface.Normal.y);
        const float hue = atan2(slope.y, slope.x) * (0.5f / 3.14159265f) + 0.5f;
        const float steepness = saturate(length(slope) * 2.5f);
        return HueToRgb(hue) * steepness;
    }
    // red: compressed (the Jacobian of the horizontal displacement below 1, crests folding). Blue: stretched (flat
    // troughs). Gray: neither
    if (mode == SURF_DEBUG_COMPRESSION)
    {
        const float jacobian = (1.0f + surface.TotalStretch.x) * (1.0f + surface.TotalStretch.y);
        const float change = jacobian - 1.0f;
        return float3(0.15f) + float3(saturate(-change * 1.5f), 0.0f, saturate(change * 1.5f));
    }
    // red: the drawn slope variance of the two finest cascades / 0.1. Green: their hidden (LEAN) variance / 0.1, which
    // grows with the pixel footprint. Blue: the long wave's phase, 1 - Jacobian / 2 of the coarse cascades, so crests
    // are bright and troughs dark. All stay linear and unclamped, so a capture of the GBuffer target gives them back
    if (mode == SURF_DEBUG_FINE_ENERGY)
    {
        const float coarseJacobian = (1.0f + surface.CoarseStretch.x) * (1.0f + surface.CoarseStretch.y);
        const float phase = 1.0f - 0.5f * coarseJacobian;
        return float3(surface.FineDrawnEnergy / 0.1f, surface.FineHiddenEnergy / 0.1f, phase);
    }
    return float3(0.0f);
}

public struct OceanSurfaceGBufferOutput
{
    public float4 GBufferNormalSigma : SV_Target0;
    public float4 GBufferFoamSss : SV_Target1;
    public float4 GBufferMotionFoamAlbedo : SV_Target2;
};

[shader("fragment")]
OceanSurfaceGBufferOutput OceanSurfaceGBufferFS(OceanSurfaceVaryings input)
{
    const bool persistent = View.PersistentFoamParameters.x > 0.5f;
    const float storedFoam = persistent ? SamplePersistentFoam(input.SurfaceXZ) : 0.0f;
    const float thresholdBias = View.PersistentFoamParameters.y * storedFoam;

    const float2 ddxXz = ddx(input.SurfaceXZ);
    const float2 ddyXz = ddy(input.SurfaceXZ);

    SurfaceSample surface;
    float breaking;
    SampleSurfaceAndWhitecaps(input.SurfaceXZ, ddxXz, ddyXz, input.WorldPosition.y, thresholdBias, surface, breaking);

    const float maxResidualArea = persistent ? saturate(max(0.0f, View.FoamResidualParameters.x) * storedFoam) : 0.0f;
    const float candidateFoam = max(breaking, maxResidualArea);

    bool needFoamSampling;
    if (OCEAN_USE_WAVE_OPS && OCEAN_WAVE_SIZE > 0u)
    {
        needFoamSampling = WaveActiveAnyTrue(candidateFoam > 1.0e-4f);
    }
    else
    {
        needFoamSampling = (candidateFoam > 1.0e-4f);
    }

    FoamMaterial material = {};
    float whitecaps = breaking;

    if (needFoamSampling)
    {
        // the whole wave samples, so that the implicit derivatives stay valid, but only a pixel with foam keeps the
        // result. Otherwise a pixel's output would depend on which pixels share its wave, which changes per frame
        const FoamMaterial sampled = SampleFoamMaterial(input.SurfaceXZ);

        if (candidateFoam > 1.0e-4f)
        {
            material = sampled;

            if (persistent && maxResidualArea > 0.0f)
            {
                const float residual = EvaluateResidualFoam(input.SurfaceXZ, ddxXz, ddyXz, storedFoam, material.Mask);
                whitecaps = max(breaking, residual);
            }
        }
    }

    const float underwaterFoam = saturate(View.ShadingParameters.x * max(breaking, saturate(View.FoamResidualParameters.x * storedFoam)));

    const float2 currNdc = (input.ClipPosition.xy / max(1e-5f, input.ClipPosition.w)) - Frame.JitterOffsetNdc;
    const float2 prevNdc = (input.PrevClipPosition.xy / max(1e-5f, input.PrevClipPosition.w)) - Frame.PrevJitterOffsetNdc;
    const float2 motionUv = (currNdc - prevNdc) * float2(0.5f, 0.5f);

    const float foamLum = (View.FoamTextureTint.y > 1e-4f) ? (material.Albedo.y / View.FoamTextureTint.y) : material.Albedo.y;

    OceanSurfaceGBufferOutput output;
    output.GBufferNormalSigma = float4(surface.Normal.x, surface.Normal.z, surface.SigmaSq.x, surface.SigmaSq.y);
    const uint surfDebugMode = GetSurfaceDebugMode();
    if (IsGBufferSurfaceDebugMode(surfDebugMode))
    {
        output.GBufferFoamSss = float4(EvaluateGBufferSurfaceDebug(surfDebugMode, surface), 1.0f);
    }
    else
    {
        output.GBufferFoamSss = float4(whitecaps, underwaterFoam, material.Roughness, foamLum);
    }
    output.GBufferMotionFoamAlbedo = float4(motionUv.x, motionUv.y, material.NormalXY.x, material.NormalXY.y);
    return output;
}
