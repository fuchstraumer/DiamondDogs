module ddEon;

// The energy-preserving Oren-Nayar (EON) diffuse BRDF of Portsmouth, Kutz and Hill, "EON: A practical
// energy-preserving rough diffuse BRDF" (JCGT, 2025). The math is theirs and follows the paper's reference code:
// the Fujii Oren-Nayar single-scatter lobe, its exact directional albedo, and the multiple-scatter compensation lobe.
// We use a polynomial acos, and clamp albedo and roughness to [0, 1].

internal static const float k_EonInvPi = 0.31830988618379067154f;
internal static const float k_FonConstant1 = 0.287813735748f; // 0.5 - 2 / (3 pi)
internal static const float k_FonConstant2 = 0.072237894236f; // 2 / 3 - 28 / (15 pi)

[ForceInline]
internal float FastAcosPositive(float x)
{
    const float cx = saturate(x);
    return sqrt(1.0f - cx) * (1.5707963f + cx * (-0.2145988f + cx * (0.0889789f - 0.0501743f * cx)));
}

internal float EvaluateFonSingleDirectionalAlbedo(float mu, float Af, float Bf)
{
    const float muClamped = saturate(mu);
    const float s = sqrt(max(0.0f, 1.0f - muClamped * muClamped));
    const float safeMu = max(1e-5f, muClamped);
    const float acosMu = FastAcosPositive(muClamped);
    const float g = s * (acosMu - s * muClamped) + (2.0f / 3.0f) * ((s / safeMu) * (1.0f - s * s * s) - s);
    return Af + (Bf * k_EonInvPi) * g;
}

/** Returns the directional albedo of the EON BRDF for albedo rho, roughness r and the cosine mu between the normal and
  * the view (or light) direction. */
public float3 EvaluateEonDirectionalAlbedo(float3 rho, float r, float mu)
{
    const float roughness = clamp(r, 0.0f, 1.0f);

    if (roughness <= 1e-4f)
    {
        return saturate(rho);
    }

    const float af = 1.0f / (1.0f + k_FonConstant1 * roughness);
    const float bf = roughness * af;
    const float avgEf = af * (1.0f + k_FonConstant2 * roughness);

    const float ef = EvaluateFonSingleDirectionalAlbedo(mu, af, bf);
    const float3 clampedRho = saturate(rho);
    const float3 denom = max(float3(1e-6f), float3(1.0f) - clampedRho * (1.0f - avgEf));
    const float3 rhoMs = (clampedRho * clampedRho) * avgEf / denom;

    return clampedRho * ef + rhoMs * max(0.0f, 1.0f - ef);
}

/** Returns the hemispherical average albedo of the EON BRDF. */
public float3 EvaluateEonAverageAlbedo(float3 rho, float r)
{
    const float roughness = clamp(r, 0.0f, 1.0f);

    if (roughness <= 1e-4f)
    {
        return saturate(rho);
    }

    const float af = 1.0f / (1.0f + k_FonConstant1 * roughness);
    const float avgEf = af * (1.0f + k_FonConstant2 * roughness);
    const float3 clampedRho = saturate(rho);
    const float3 denom = max(float3(1e-6f), float3(1.0f) - clampedRho * (1.0f - avgEf));
    return (clampedRho * avgEf) / denom;
}

/** Returns the EON BRDF times N.L for one light direction. Multiply by the light's radiance (or irradiance at normal
  * incidence) to get the reflected radiance. */
public float3 EvaluateEonDirect(float3 rho, float r, float3 N, float3 V, float3 L)
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
        return clampedRho * (k_EonInvPi * nDotL);
    }

    const float af = 1.0f / (1.0f + k_FonConstant1 * roughness);
    const float bf = roughness * af;
    const float avgEf = af * (1.0f + k_FonConstant2 * roughness);

    const float s = dot(V, L) - nDotL * nDotV;
    const float sOverT = s > 0.0f ? s / max(nDotL, nDotV) : s;
    const float3 fSs = (clampedRho * k_EonInvPi) * (af * (1.0f + roughness * sOverT));

    const float efL = EvaluateFonSingleDirectionalAlbedo(nDotL, af, bf);
    const float efV = EvaluateFonSingleDirectionalAlbedo(nDotV, af, bf);

    const float3 denomMs = max(float3(1e-6f), float3(1.0f) - clampedRho * (1.0f - avgEf));
    const float3 rhoMs = (clampedRho * clampedRho) * avgEf / denomMs;

    const float compL = max(0.0f, 1.0f - efL);
    const float compV = max(0.0f, 1.0f - efV);
    const float denomAvg = max(1e-6f, 1.0f - avgEf);
    const float3 fMs = (rhoMs * k_EonInvPi) * ((compL * compV) / denomAvg);

    return (fSs + fMs) * nDotL;
}

/** Returns the radiance that the EON BRDF reflects toward V under the given irradiance, for example from
  * EvaluateShIrradiance. Reflected radiance is the directional albedo times the mean incident radiance, irradiance / pi.
  */
public float3 EvaluateEonIbl(float3 rho, float r, float3 N, float3 V, float3 irradiance)
{
    const float nDotV = saturate(dot(N, V));
    return EvaluateEonDirectionalAlbedo(rho, r, nDotV) * (irradiance * k_EonInvPi);
}

/** Returns the irradiance at normal N from nine L2 spherical harmonic coefficients of radiance, with Ramamoorthi and
  * Hanrahan's closed form (2001). The coefficients must be radiance: the c1-c5 constants already apply the cosine lobe.
  * Basis order: 1, y, z, x, xy, yz, 3z^2 - 1, xz, x^2 - y^2. */
public float3 EvaluateShIrradiance(float3 N, float4 shCoeffs[9])
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
