implementing OceanRender;

// Portsmouth et al. (2024) Energy-preserving Oren-Nayar (EON) diffuse BRDF module.
// Provides analytical directional and average albedo evaluations, exact single-scattering
// and multiple-scattering compensation, L2 spherical harmonics irradiance evaluation in AP1,
// and equirectangular sky sampling with gradient footprints.

internal static const float EON_PI = 3.14159265358979323846f;
internal static const float EON_INV_PI = 0.31830988618379067154f;
internal static const float EON_INV_2PI = 0.15915494309189533576f;
internal static const float EON_CONST1_FON = 0.287813735748f; // 0.5 - 2.0 / (3.0 * pi)
internal static const float EON_CONST2_FON = 0.072237894236f; // 2.0 / 3.0 - 28.0 / (15.0 * pi)

[ForceInline]
internal float FastAcosPositive(float x)
{
    const float cx = saturate(x);
    return sqrt(1.0f - cx) * (1.5707963f + cx * (-0.2145988f + cx * (0.0889789f - 0.0501743f * cx)));
}

[ForceInline]
internal float FastAcos(float x)
{
    return acos(clamp(x, -1.0f, 1.0f));
}

internal float EvaluateFonSingleDirectionalAlbedo(float mu, float r, float Af, float Bf)
{
    const float muClamped = saturate(mu);
    const float s = sqrt(max(0.0f, 1.0f - muClamped * muClamped));
    const float safeMu = max(1e-5f, muClamped);
    const float acosMu = FastAcosPositive(muClamped);
    const float g = s * (acosMu - s * muClamped) + (2.0f / 3.0f) * ((s / safeMu) * (1.0f - s * s * s) - s);
    return Af + (Bf * EON_INV_PI) * g;
}

internal float3 EvaluateEonDirectionalAlbedo(float3 rho, float r, float mu)
{
    const float roughness = clamp(r, 0.0f, 1.0f);
    if (roughness <= 1e-4f)
    {
        return saturate(rho);
    }

    const float af = 1.0f / (1.0f + EON_CONST1_FON * roughness);
    const float bf = roughness * af;
    const float avgEf = af * (1.0f + EON_CONST2_FON * roughness);

    const float ef = EvaluateFonSingleDirectionalAlbedo(mu, roughness, af, bf);
    const float3 clampedRho = saturate(rho);
    const float3 denom = max(float3(1e-6f), float3(1.0f) - clampedRho * (1.0f - avgEf));
    const float3 rhoMs = (clampedRho * clampedRho) * avgEf / denom;

    return clampedRho * ef + rhoMs * max(0.0f, 1.0f - ef);
}

internal float3 EvaluateEonAverageAlbedo(float3 rho, float r)
{
    const float roughness = clamp(r, 0.0f, 1.0f);
    if (roughness <= 1e-4f)
    {
        return saturate(rho);
    }

    const float af = 1.0f / (1.0f + EON_CONST1_FON * roughness);
    const float avgEf = af * (1.0f + EON_CONST2_FON * roughness);
    const float3 clampedRho = saturate(rho);
    const float3 denom = max(float3(1e-6f), float3(1.0f) - clampedRho * (1.0f - avgEf));
    return (clampedRho * avgEf) / denom;
}

internal float3 EvaluateEonDirect(float3 rho, float r, float3 N, float3 V, float3 L)
{
    const float nDotL = saturate(dot(N, L));
    const float nDotV = saturate(dot(N, V));

    if (nDotL <= 1e-6f || nDotV <= 1e-6f)
    {
        return float3(0.0f);
    }

    const float roughness = clamp(r, 0.0f, 1.0f);
    const float3 clampedRho = saturate(rho);

    if (roughness <= 1e-4f)
    {
        return clampedRho * (EON_INV_PI * nDotL);
    }

    const float af = 1.0f / (1.0f + EON_CONST1_FON * roughness);
    const float bf = roughness * af;
    const float avgEf = af * (1.0f + EON_CONST2_FON * roughness);

    const float s = dot(V, L) - nDotL * nDotV;
    const float sOverT = s > 0.0f ? s / max(nDotL, nDotV) : s;
    const float3 fSs = (clampedRho * EON_INV_PI) * (af * (1.0f + roughness * sOverT));

    const float efL = EvaluateFonSingleDirectionalAlbedo(nDotL, roughness, af, bf);
    const float efV = EvaluateFonSingleDirectionalAlbedo(nDotV, roughness, af, bf);

    const float3 denomMs = max(float3(1e-6f), float3(1.0f) - clampedRho * (1.0f - avgEf));
    const float3 rhoMs = (clampedRho * clampedRho) * avgEf / denomMs;

    const float compL = max(0.0f, 1.0f - efL);
    const float compV = max(0.0f, 1.0f - efV);
    const float denomAvg = max(1e-6f, 1.0f - avgEf);
    const float3 fMs = (rhoMs * EON_INV_PI) * ((compL * compV) / denomAvg);

    return (fSs + fMs) * nDotL;
}

internal float3 EvaluateEonIbl(float3 rho, float r, float3 N, float3 V, float3 irradiance)
{
    const float nDotV = saturate(dot(N, V));
    return EvaluateEonDirectionalAlbedo(rho, r, nDotV) * irradiance;
}

internal float3 EvaluateSkyShIrradiance(float3 N, float4 shCoeffs[9])
{
    const float c1 = 0.429043f;
    const float c2 = 0.511664f;
    const float c3 = 0.743125f;
    const float c4 = 0.886227f;
    const float c5 = 0.247708f;

    const float3 irradiance = c4 * shCoeffs[0].xyz
                            + 2.0f * c2 * (shCoeffs[3].xyz * N.x + shCoeffs[1].xyz * N.y + shCoeffs[2].xyz * N.z)
                            + c1 * shCoeffs[8].xyz * (N.x * N.x - N.y * N.y)
                            + c3 * shCoeffs[6].xyz * (N.z * N.z) - c5 * shCoeffs[6].xyz
                            + 2.0f * c1 * (shCoeffs[4].xyz * N.x * N.y + shCoeffs[7].xyz * N.x * N.z + shCoeffs[5].xyz * N.y * N.z);

    return max(float3(0.0f), irradiance);
}

[ForceInline]
internal float3 SampleEquirectangularSkyGrad(Texture2D<float4> skyTex, SamplerState skySampler, float3 dir, float2 ddxUv, float2 ddyUv, float roughnessBias)
{
    const float phi = atan2(dir.z, dir.x);
    const float theta = FastAcos(clamp(dir.y, -1.0f, 1.0f));
    const float2 uv = float2(phi * EON_INV_2PI + 0.5f, theta * EON_INV_PI);

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
