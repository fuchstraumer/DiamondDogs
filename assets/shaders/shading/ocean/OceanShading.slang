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
    float sumKSq = 0.0f;

    [unroll]
    for (int cascade = 0; cascade < OCEAN_CASCADE_COUNT; ++cascade)
    {
        const float weight = CascadeDistanceWeight(cascade, distance);
        if (weight > 0.0f)
        {
            const float3 uvw = float3(CascadeUv(surface_xz, cascade), float(cascade));
            // use anisotropic filtering on this texture: we already have such a sampler for the derivatives
            const float2 moments = Whitecaps.Sample(OceanSampler, uvw).xy;
            muA += moments.x * weight;
            sumKSq += moments.y * weight;
        }
    }

    // footprint variance (sigma^2 = E[k^2] - (E[k])^2
    const float sigmaSq = max(1e-6f, sumKSq - muA * muA);
    const float sigmaA = sqrt(sigmaSq);
    // TODO: Get this in the sea state buffer!! Soon!
    // wave breaking threshold: [-0.2, 0.2]. depends on wind and steepness
    // lower = only violent folding breaks higer = broader whitecaps
    const float epsilon = -0.05f;
    // closed-form guassian integral over the pixel footprint given by whitecaps value
    // 0.70710678 = sqrt(2)/2 here
    const float errfIn = 0.70710678f * (epsilon - muA) / sigmaA;
    const float whitecapCoverage = saturate(0.5f + (0.5f * FastErf(errfIn)));
    return whitecapCoverage;
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

            totalSlope += derivatives.xy;
            totalStretch += derivatives.zw;

            // Jacobian in displacement.w: calm water is 1.0, crest compression is < 1.0, folding occurs below ~0.3.
            // Only highlight breaking crests where surface is folded AND height is positive.
            // Cascade 0 (swell) is a long roller that rarely whitecaps on its own; cascades 1-3 provide steep chop.
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
    const float sun = pow(saturate(dot(surface.Normal, halfVector)), 400.0f) * 8.0f;

    float3 color = lerp(deepColor, skyColor, fresnel) + sun.xxx;

    // evaluate whitecap coverage based on the precomputed moments
    const float distance = length(input.SurfaceXZ - View.CameraPositionTime.xz);
    const float whitecaps = EvaluateWhitecapCoverage(input.SurfaceXZ, distance);
    // evaluated as classic lambertian diffuse with albedo 0.40
    const float nDotL = saturate(dot(surface.Normal, toSun));
    const float3 foamAlbedo = 0.40f.xxx;
    const float3 foamRadiance = foamAlbedo * (nDotL * float3(1.0f, 0.95f, 0.9f) + skyColor * 0.5f);

    color = lerp(color, foamRadiance, whitecaps);
    // partly transparent, so the seabed shows through while the real shading is written
    return float4(color, 0.9f);
}
