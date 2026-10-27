implementing OceanRender;

// PLACEHOLDER shading, only so the demo shows something: the author is writing the real shading model. Replace
// OceanSurfaceFS freely; the vertex stage only promises OceanSurfaceVaryings and the bindings in OceanRenderCommon.

internal struct SurfaceSample
{
    float3 Normal;
    // 0 where the surface is calm, towards 1 where the horizontal displacement compresses or folds it
    float Foam;
};

// FastErf and EvaluateWhitecapCoverage live in OceanWhitecaps.slang, shared with the persistent foam

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
    // the persistent foam raises the breaking threshold, so foam near a crest keeps the whitecaps' detail, and the
    // residual term draws the old foam left behind once the crest has moved on
    const bool persistent = View.PersistentFoamParameters.x > 0.5f;
    const float storedFoam = persistent ? SamplePersistentFoam(input.SurfaceXZ) : 0.0f;
    const float2 footprintX = ddx(input.SurfaceXZ);
    const float2 footprintY = ddy(input.SurfaceXZ);
    const float breaking = EvaluateWhitecapCoverage(input.SurfaceXZ, footprintX, footprintY, View.PersistentFoamParameters.y * storedFoam);
    const float residual = persistent ? EvaluateResidualFoam(input.SurfaceXZ, footprintX, footprintY, storedFoam) : 0.0f;
    const float whitecaps = max(breaking, residual);
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
