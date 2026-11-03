implementing OceanRender;

// Equirectangular sky sampling with gradient footprints. The EON BRDF and the SH irradiance are in the ddEon module.

internal static const float k_SkyInvPi = 0.31830988618379067154f;
internal static const float k_SkyInv2Pi = 0.15915494309189533576f;

[ForceInline]
internal float FastAcos(float x)
{
    return acos(clamp(x, -1.0f, 1.0f));
}

[ForceInline]
internal float3 SampleEquirectangularSkyGrad(Texture2D<float4> skyTex, SamplerState skySampler, float3 dir, float2 ddxUv, float2 ddyUv, float roughnessBias)
{
    const float phi = atan2(dir.z, dir.x);
    const float theta = FastAcos(clamp(dir.y, -1.0f, 1.0f));
    const float2 uv = float2(phi * k_SkyInv2Pi + 0.5f, theta * k_SkyInvPi);

    // Wrap horizontal UV derivatives across the [-0.5, 0.5] periodic boundary
    // to prevent seam line artifacts when crossing phi = +-PI
    float2 ddx = ddxUv;
    ddx.x -= round(ddx.x);
    float2 ddy = ddyUv;
    ddy.x -= round(ddy.x);

    // Clamp gradient magnitude to prevent polar singularities
    const float maxGrad = 0.25f;
    ddx = clamp(ddx, float2(-maxGrad), float2(maxGrad));
    ddy = clamp(ddy, float2(-maxGrad), float2(maxGrad));

    const float gradScale = exp2(roughnessBias);
    ddx *= gradScale;
    ddy *= gradScale;

    return skyTex.SampleGrad(skySampler, uv, ddx, ddy).xyz;
}
