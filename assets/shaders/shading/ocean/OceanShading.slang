implementing OceanRender;

// PLACEHOLDER shading, only so the demo shows something: the author is writing the real shading model. Replace
// OceanSurfaceFS freely; the vertex stage only promises OceanSurfaceVaryings and the bindings in OceanRenderCommon.

internal struct SurfaceSample
{
    float3 Normal;
    // 0 where the surface is calm, towards 1 where the horizontal displacement compresses or folds it
    float Foam;
};

internal SurfaceSample SampleSurface(float2 surface_xz)
{
    float2 totalSlope = float2(0.0f);
    float2 totalStretch = float2(0.0f);
    float foam = 0.0f;
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
            foam += fold * crest * weight * 2.0f;
        }
    }
    SurfaceSample result;
    // Clamping the denominator prevents division by zero and normal spikes when waves fold
    const float2 slope = totalSlope / max(float2(0.1f), 1.0f + totalStretch);
    result.Normal = normalize(float3(-slope.x, 1.0f, -slope.y));
    result.Foam = saturate(foam);
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
    color = lerp(color, float3(0.85f), surface.Foam);
    // partly transparent, so the seabed shows through while the real shading is written
    return float4(color, 0.9f);
}
