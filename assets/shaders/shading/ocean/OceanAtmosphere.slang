module OceanAtmosphere;

import LodestoneAttributes;
import LodestoneShims;
import ddColor;

// OceanAtmosphere.slang
// Self-contained Bruneton & Neyret (2008) 4-LUT precomputed atmospheric scattering model.
// Evaluates Rayleigh scattering, anisotropic Mie aerosol scattering, and stratospheric Ozone absorption.
// Precomputes Transmittance (256x64), Irradiance (64x16), and 4D Inscatter (32x128x256 packed into 3D).
// Exports clean public runtime evaluation functions converting Rec.709 spectral radiance to AP1 linear space.

public static const float ATMO_PI = 3.14159265358979323846f;

// Atmosphere physical parameters
public struct AtmosphereParameters
{
    public float BottomRadiusKm;        // Rg: Earth surface radius (6360.0 km)
    public float TopRadiusKm;           // Rt: Top-of-atmosphere boundary (6420.0 km)
    public float RayleighScaleHeightKm; // HR: Rayleigh density scale height (8.0 km)
    public float MieScaleHeightKm;      // HM: Mie aerosol scale height (1.2 km)
    public float3 RayleighScattering;   // beta_R^s at sea level (5.802e-3, 13.558e-3, 33.1e-3 km^-1)
    public float MieAnisotropy;         // g: Cornette-Shanks asymmetry factor (0.8)
    public float3 MieScattering;        // beta_M^s at sea level (3.996e-3, 3.996e-3, 3.996e-3 km^-1)
    public float MieExtinction;         // beta_M^e at sea level (4.440e-3 km^-1)
    public float3 OzoneAbsorption;      // beta_O^a at peak (0.650e-3, 1.881e-3, 0.085e-3 km^-1)
    public float OzoneCenterKm;         // Ozone layer peak altitude (25.0 km)
    public float OzoneWidthKm;          // Ozone layer half-width (15.0 km)
    public float GroundAlbedo;          // Average ground reflectance alpha (0.1)
    public float2 Pad0;
    public float3 SolarIrradiance;       // Top-of-atmosphere solar irradiance E_sun
    public uint ScatteringOrder;        // Precomputation scattering order k >= 2
};

// Return type for aerial perspective evaluation
public struct AerialPerspectiveResult
{
    public float3 Transmittance;
    public float3 InscatterAP1;
};

// -------------------------------------------------------------------------------------------------
// Resource Declarations with explicit Lodestone extent attributes
// -------------------------------------------------------------------------------------------------

// Transmittance LUT: 256 (u_mu) x 64 (u_r)
[format("rgba16f")]
[ls_extent_2d("256", "64")]
RWTexture2D<float4> TransmittanceLutWrite;

[ls_extent_2d("256", "64")]
Texture2D<float4> TransmittanceLut;

// Irradiance LUT: 64 (u_mu_s) x 16 (u_r)
[format("rgba16f")]
[ls_extent_2d("64", "16")]
RWTexture2D<float4> IrradianceLutWrite;

[ls_extent_2d("64", "16")]
Texture2D<float4> IrradianceLut;

[format("rgba16f")]
[ls_extent_2d("64", "16")]
RWTexture2D<float4> DeltaIrradianceLutWrite;

[ls_extent_2d("64", "16")]
Texture2D<float4> DeltaIrradianceLut;

// 4D Inscatter LUT packed into 3D: 256 (u_mu_s x u_nu) x 128 (u_mu) x 32 (u_r)
[format("rgba16f")]
[ls_extent_3d("256", "128", "32")]
RWTexture3D<float4> InscatterLutWrite;

[ls_extent_3d("256", "128", "32")]
Texture3D<float4> InscatterLut;

[format("rgba16f")]
[ls_extent_3d("256", "128", "32")]
RWTexture3D<float4> DeltaRayleighLutWrite;

[ls_extent_3d("256", "128", "32")]
Texture3D<float4> DeltaRayleighLut;

[format("rgba16f")]
[ls_extent_3d("256", "128", "32")]
RWTexture3D<float4> DeltaMieLutWrite;

[ls_extent_3d("256", "128", "32")]
Texture3D<float4> DeltaMieLut;

[format("rgba16f")]
[ls_extent_3d("256", "128", "32")]
RWTexture3D<float4> DeltaScatteringDensityLutWrite;

[ls_extent_3d("256", "128", "32")]
Texture3D<float4> DeltaScatteringDensityLut;

[format("rgba16f")]
[ls_extent_3d("256", "128", "32")]
RWTexture3D<float4> DeltaMultipleScatteringLutWrite;

[ls_extent_3d("256", "128", "32")]
Texture3D<float4> DeltaMultipleScatteringLut;

SamplerState AtmosphereLinearSampler;

// -------------------------------------------------------------------------------------------------
// Phase Functions
// -------------------------------------------------------------------------------------------------

public float PhaseFunctionRayleigh(float nu)
{
    return (3.0f / (16.0f * ATMO_PI)) * (1.0f + nu * nu);
}

public float PhaseFunctionMie(float nu, float g)
{
    const float g2 = g * g;
    const float denom = 1.0f + g2 - 2.0f * g * nu;
    return (3.0f / (8.0f * ATMO_PI)) * ((1.0f - g2) * (1.0f + nu * nu)) /
           ((2.0f + g2) * pow(max(1e-4f, denom), 1.5f));
}

// -------------------------------------------------------------------------------------------------
// Coordinate Mapping Helpers
// -------------------------------------------------------------------------------------------------

// Converts physical (r, mu) to normalized 2D texture coordinates for TransmittanceLut
[ForceInline]
public float2 GetTransmittanceUV(AtmosphereParameters params, float r, float mu)
{
    const float Rg = params.BottomRadiusKm;
    const float Rt = params.TopRadiusKm;
    const float H = sqrt(max(0.0f, Rt * Rt - Rg * Rg));
    const float rho = sqrt(max(0.0f, r * r - Rg * Rg));

    const float delta = r * r * (mu * mu - 1.0f) + Rt * Rt;
    const float d = max(0.0f, -r * mu + sqrt(max(0.0f, delta)));

    const float d_min = Rt - r;
    const float d_max = rho + H;

    const float u_mu = clamp((d - d_min) / max(1e-4f, d_max - d_min), 0.0f, 1.0f);
    const float u_r = clamp(rho / max(1e-4f, H), 0.0f, 1.0f);
    return float2(u_mu, u_r);
}

// Converts normalized (u_mu, u_r) back to physical (r, mu)
[ForceInline]
public void GetRadiusAndMuFromTransmittanceUV(
    AtmosphereParameters params,
    float2 uv,
    out float r,
    out float mu)
{
    const float Rg = params.BottomRadiusKm;
    const float Rt = params.TopRadiusKm;
    const float H = sqrt(max(0.0f, Rt * Rt - Rg * Rg));
    const float rho = uv.y * H;

    r = sqrt(Rg * Rg + rho * rho);
    r = clamp(r, Rg, Rt);

    const float d_min = Rt - r;
    const float d_max = rho + H;
    const float d = d_min + uv.x * (d_max - d_min);

    mu = (Rt * Rt - r * r - d * d) / max(1e-4f, 2.0f * r * d);
    mu = clamp(mu, -1.0f, 1.0f);
}

// Converts physical (r, mu_s) to normalized 2D texture coordinates for IrradianceLut
[ForceInline]
public float2 GetIrradianceUV(AtmosphereParameters params, float r, float mu_s)
{
    const float Rg = params.BottomRadiusKm;
    const float Rt = params.TopRadiusKm;
    const float u_r = clamp((r - Rg) / max(1e-4f, Rt - Rg), 0.0f, 1.0f);
    const float u_mu_s = clamp((mu_s + 1.0f) * 0.5f, 0.0f, 1.0f);
    return float2(u_mu_s, u_r);
}

// Converts normalized (u_mu_s, u_r) back to physical (r, mu_s)
[ForceInline]
public void GetRadiusAndMuSFromIrradianceUV(
    AtmosphereParameters params,
    float2 uv,
    out float r,
    out float mu_s)
{
    const float Rg = params.BottomRadiusKm;
    const float Rt = params.TopRadiusKm;
    r = Rg + uv.y * (Rt - Rg);
    mu_s = 2.0f * uv.x - 1.0f;
    mu_s = clamp(mu_s, -1.0f, 1.0f);
}

// Converts physical (r, mu, mu_s, nu) to normalized 4D coordinates
[ForceInline]
public float4 GetInscatterUV(
    AtmosphereParameters params,
    float r,
    float mu,
    float mu_s,
    float nu)
{
    const float Rg = params.BottomRadiusKm;
    const float Rt = params.TopRadiusKm;
    const float H = sqrt(max(0.0f, Rt * Rt - Rg * Rg));
    const float rho = sqrt(max(0.0f, r * r - Rg * Rg));
    const float mu_horiz = (r > 0.0f) ? (-rho / r) : 0.0f;

    float u_mu;
    if (mu < mu_horiz)
    {
        // Viewing ray intersects the ground
        const float delta_g = r * r * (mu * mu - 1.0f) + Rg * Rg;
        const float d = -r * mu - sqrt(max(0.0f, delta_g));
        const float d_min = r - Rg;
        const float d_max = rho;
        const float denom = max(1e-4f, d_max - d_min);
        u_mu = clamp(0.5f - 0.5f * ((d - d_min) / denom), 0.0f, 0.499999f);
    }
    else
    {
        // Viewing ray exits top of atmosphere
        const float delta_t = r * r * (mu * mu - 1.0f) + Rt * Rt;
        const float d = -r * mu + sqrt(max(0.0f, delta_t));
        const float d_min = Rt - r;
        const float d_max = rho + H;
        const float denom = max(1e-4f, d_max - d_min);
        u_mu = clamp(0.5f + 0.5f * ((d - d_min) / denom), 0.5f, 1.0f);
    }

    const float u_r = clamp(rho / max(1e-4f, H), 0.0f, 1.0f);
    const float u_mu_s = clamp((1.0f - exp(-3.0f * mu_s - 0.6f)) / (1.0f - exp(-3.6f)), 0.0f, 1.0f);
    const float u_nu = clamp((1.0f + nu) * 0.5f, 0.0f, 1.0f);

    return float4(u_mu_s, u_nu, u_mu, u_r);
}

// Decodes thread coordinates (x in [0..255], y in [0..127], z in [0..31]) to physical (r, mu, mu_s, nu, d_ray)
[ForceInline]
public void GetPhysicalFromInscatterTexel(
    AtmosphereParameters params,
    uint3 id,
    out float r,
    out float mu,
    out float mu_s,
    out float nu,
    out float d_ray)
{
    const float Rg = params.BottomRadiusKm;
    const float Rt = params.TopRadiusKm;
    const float H = sqrt(max(0.0f, Rt * Rt - Rg * Rg));

    const float u_r = (float(id.z) + 0.5f) / 32.0f;
    const float rho = u_r * H;
    r = sqrt(Rg * Rg + rho * rho);
    r = clamp(r, Rg, Rt);

    const float u_mu = (float(id.y) + 0.5f) / 128.0f;
    if (u_mu < 0.5f)
    {
        // Ray hits ground
        const float d_min = r - Rg;
        const float d_max = rho;
        const float d = d_min + (1.0f - 2.0f * u_mu) * (d_max - d_min);
        mu = (d > 1e-4f) ? ((Rg * Rg - r * r - d * d) / (2.0f * r * d)) : -1.0f;
        mu = clamp(mu, -1.0f, 1.0f);
        d_ray = d;
    }
    else
    {
        // Ray exits atmosphere
        const float d_min = Rt - r;
        const float d_max = rho + H;
        const float d = d_min + (2.0f * u_mu - 1.0f) * (d_max - d_min);
        mu = (d > 1e-4f) ? ((Rt * Rt - r * r - d * d) / (2.0f * r * d)) : 1.0f;
        mu = clamp(mu, -1.0f, 1.0f);
        d_ray = d;
    }

    // Horizontal tile index: 8 tiles for nu across x, each tile has 32 columns for mu_s
    const uint tile_nu = id.x / 32u;
    const uint offset_mu_s = id.x % 32u;

    const float u_nu = (float(tile_nu) + 0.5f) / 8.0f;
    nu = 2.0f * u_nu - 1.0f;

    const float u_mu_s = (float(offset_mu_s) + 0.5f) / 32.0f;
    mu_s = (-log(max(1e-5f, 1.0f - u_mu_s * (1.0f - exp(-3.6f)))) - 0.6f) / 3.0f;
    mu_s = clamp(mu_s, -1.0f, 1.0f);

    // Clamp nu to physically realizable range for (mu, mu_s)
    const float sinMu = sqrt(max(0.0f, 1.0f - mu * mu));
    const float sinMuS = sqrt(max(0.0f, 1.0f - mu_s * mu_s));
    nu = clamp(nu, mu * mu_s - sinMu * sinMuS, mu * mu_s + sinMu * sinMuS);
}

// -------------------------------------------------------------------------------------------------
// Texture Sampling Helpers
// -------------------------------------------------------------------------------------------------

[ForceInline]
public float3 SampleTransmittanceLut(
    Texture2D<float4> lut,
    SamplerState samp,
    AtmosphereParameters params,
    float r,
    float mu)
{
    const float2 uv = GetTransmittanceUV(params, r, mu);
    return lut.SampleLevel(samp, uv, 0.0f).rgb;
}

[ForceInline]
public float3 SampleIrradianceLut(
    Texture2D<float4> lut,
    SamplerState samp,
    AtmosphereParameters params,
    float r,
    float mu_s)
{
    const float2 uv = GetIrradianceUV(params, r, mu_s);
    return lut.SampleLevel(samp, uv, 0.0f).rgb;
}

[ForceInline]
public float4 SampleInscatterLut4D(
    Texture3D<float4> lut,
    SamplerState samp,
    AtmosphereParameters params,
    float r,
    float mu,
    float mu_s,
    float nu)
{
    const float4 uv4 = GetInscatterUV(params, r, mu, mu_s, nu);
    const float u_mu_s = uv4.x;
    const float u_nu = uv4.y;
    const float u_mu = uv4.z;
    const float u_r = uv4.w;

    // Map into 3D texture dimensions (256 x 128 x 32) with quadrilinear interpolation across nu tiles
    const float f_nu = u_nu * 7.0f;
    const float tile_0 = floor(f_nu);
    const float frac_nu = f_nu - tile_0;
    const float tile_1 = min(7.0f, tile_0 + 1.0f);

    const float x_local = (u_mu_s * 31.0f + 0.5f);
    const float u_0 = (tile_0 * 32.0f + x_local) / 256.0f;
    const float u_1 = (tile_1 * 32.0f + x_local) / 256.0f;

    const float v = (u_mu * 127.0f + 0.5f) / 128.0f;
    const float w = (u_r * 31.0f + 0.5f) / 32.0f;

    const float4 s0 = lut.SampleLevel(samp, float3(u_0, v, w), 0.0f);
    const float4 s1 = lut.SampleLevel(samp, float3(u_1, v, w), 0.0f);
    return lerp(s0, s1, frac_nu);
}

// -------------------------------------------------------------------------------------------------
// 1. AtmosphereTransmittanceCS
// Computes optical depth and spectral transmittance T(r, mu) into 256x64 RGBA16F table.
// -------------------------------------------------------------------------------------------------

[shader("compute")]
[numthreads(8, 8, 1)]
void AtmosphereTransmittanceCS(
    uint3 id : SV_DispatchThreadID,
    uniform AtmosphereParameters params)
{
    if (id.x >= 256u || id.y >= 64u)
    {
        return;
    }

    const float2 uv = (float2(id.xy) + 0.5f) / float2(256.0f, 64.0f);
    float r, mu;
    GetRadiusAndMuFromTransmittanceUV(params, uv, r, mu);

    const float Rg = params.BottomRadiusKm;
    const float Rt = params.TopRadiusKm;
    const float H = sqrt(max(0.0f, Rt * Rt - Rg * Rg));
    const float rho = uv.y * H;

    const float d_min = Rt - r;
    const float d_max = rho + H;
    const float d = d_min + uv.x * (d_max - d_min);

    // Trapezoidal numerical integration of optical depth along ray of length d
    const uint NUM_STEPS = 500u;
    const float ds = d / float(NUM_STEPS);
    float3 opticalDepth = float3(0.0f, 0.0f, 0.0f);

    for (uint i = 0u; i < NUM_STEPS; ++i)
    {
        const float s = (float(i) + 0.5f) * ds;
        const float r_s = sqrt(max(Rg * Rg, r * r + 2.0f * r * s * mu + s * s));
        const float h_s = r_s - Rg;

        const float rho_R = exp(-h_s / params.RayleighScaleHeightKm);
        const float rho_M = exp(-h_s / params.MieScaleHeightKm);
        const float rho_O = max(0.0f, 1.0f - abs(h_s - params.OzoneCenterKm) / params.OzoneWidthKm);

        opticalDepth += (params.RayleighScattering * rho_R +
                         params.MieExtinction * rho_M +
                         params.OzoneAbsorption * rho_O) * ds;
    }

    const float3 transmittance = exp(-opticalDepth);
    TransmittanceLutWrite[id.xy] = float4(transmittance, 1.0f);
}

// -------------------------------------------------------------------------------------------------
// 2. AtmosphereDirectIrradianceCS
// Computes direct solar ground/ocean irradiance Delta E(r, mu_s) and initializes IrradianceLut.
// -------------------------------------------------------------------------------------------------

[shader("compute")]
[numthreads(8, 8, 1)]
void AtmosphereDirectIrradianceCS(
    uint3 id : SV_DispatchThreadID,
    uniform AtmosphereParameters params)
{
    if (id.x >= 64u || id.y >= 16u)
    {
        return;
    }

    const float2 uv = (float2(id.xy) + 0.5f) / float2(64.0f, 16.0f);
    float r, mu_s;
    GetRadiusAndMuSFromIrradianceUV(params, uv, r, mu_s);

    const float Rg = params.BottomRadiusKm;
    const float sinHorizonSq = 1.0f - (Rg * Rg) / (r * r);
    const float cosHorizon = -sqrt(max(0.0f, sinHorizonSq));

    float3 deltaE = float3(0.0f, 0.0f, 0.0f);
    if (mu_s >= cosHorizon)
    {
        const float3 T_sun = SampleTransmittanceLut(TransmittanceLut, AtmosphereLinearSampler, params, r, mu_s);
        deltaE = params.SolarIrradiance * T_sun * max(0.0f, mu_s);
    }

    DeltaIrradianceLutWrite[id.xy] = float4(deltaE, 0.0f);
    IrradianceLutWrite[id.xy] = float4(0.0f, 0.0f, 0.0f, 0.0f);
}

// -------------------------------------------------------------------------------------------------
// 3. AtmosphereSingleScatteringCS
// Computes single Rayleigh and Mie scattering into 3D textures (256x128x32 RGBA16F).
// -------------------------------------------------------------------------------------------------

[shader("compute")]
[numthreads(8, 8, 4)]
void AtmosphereSingleScatteringCS(
    uint3 id : SV_DispatchThreadID,
    uniform AtmosphereParameters params)
{
    if (id.x >= 256u || id.y >= 128u || id.z >= 32u)
    {
        return;
    }

    float r, mu, mu_s, nu, d_ray;
    GetPhysicalFromInscatterTexel(params, id, r, mu, mu_s, nu, d_ray);

    const float Rg = params.BottomRadiusKm;
    const uint NUM_STEPS = 50u;
    const float ds = d_ray / float(NUM_STEPS);

    float3 deltaSR = float3(0.0f, 0.0f, 0.0f);
    float3 deltaSM = float3(0.0f, 0.0f, 0.0f);

    for (uint i = 0u; i < NUM_STEPS; ++i)
    {
        const float s = (float(i) + 0.5f) * ds;
        const float r_s = sqrt(max(Rg * Rg, r * r + 2.0f * r * s * mu + s * s));
        const float h_s = r_s - Rg;

        const float mu_v_s = (r * mu + s) / max(1e-4f, r_s);
        const float mu_s_s = (r * mu_s + s * nu) / max(1e-4f, r_s);

        const float sinHorizSq = 1.0f - (Rg * Rg) / (r_s * r_s);
        const float cosHoriz = -sqrt(max(0.0f, sinHorizSq));

        if (mu_s_s >= cosHoriz)
        {
            const float3 T_sun = SampleTransmittanceLut(TransmittanceLut, AtmosphereLinearSampler, params, r_s, mu_s_s);
            float3 T_view;
            const float mu_horiz_r = -sqrt(max(0.0f, 1.0f - (Rg * Rg) / (r * r)));
            if (mu < mu_horiz_r)
            {
                const float3 T_samp = SampleTransmittanceLut(TransmittanceLut, AtmosphereLinearSampler, params, r_s, -mu_v_s);
                const float3 T_cam = SampleTransmittanceLut(TransmittanceLut, AtmosphereLinearSampler, params, r, -mu);
                T_view = clamp(T_samp / max(float3(1e-4f), T_cam), float3(0.0f), float3(1.0f));
            }
            else
            {
                const float3 T_cam = SampleTransmittanceLut(TransmittanceLut, AtmosphereLinearSampler, params, r, mu);
                const float3 T_samp = SampleTransmittanceLut(TransmittanceLut, AtmosphereLinearSampler, params, r_s, mu_v_s);
                T_view = clamp(T_cam / max(float3(1e-4f), T_samp), float3(0.0f), float3(1.0f));
            }

            const float rho_R = exp(-h_s / params.RayleighScaleHeightKm);
            const float rho_M = exp(-h_s / params.MieScaleHeightKm);

            deltaSR += T_view * T_sun * rho_R * ds;
            deltaSM += T_view * T_sun * rho_M * ds;
        }
    }

    deltaSR *= params.RayleighScattering * params.SolarIrradiance;
    deltaSM *= params.MieScattering * params.SolarIrradiance;

    DeltaRayleighLutWrite[id] = float4(deltaSR, 0.0f);
    DeltaMieLutWrite[id] = float4(deltaSM, 0.0f);

    // Store combined (C_*, C_M,r) into InscatterLutWrite
    InscatterLutWrite[id] = float4(deltaSR, deltaSM.r);
}

// -------------------------------------------------------------------------------------------------
// 4. AtmosphereScatteringDensityCS
// Computes higher-order scattering density Delta J from previous order Delta E and Delta S.
// -------------------------------------------------------------------------------------------------

[shader("compute")]
[numthreads(8, 8, 4)]
void AtmosphereScatteringDensityCS(
    uint3 id : SV_DispatchThreadID,
    uniform AtmosphereParameters params)
{
    if (id.x >= 256u || id.y >= 128u || id.z >= 32u)
    {
        return;
    }

    float r, mu, mu_s, nu, d_ray;
    GetPhysicalFromInscatterTexel(params, id, r, mu, mu_s, nu, d_ray);

    const float Rg = params.BottomRadiusKm;
    const float sinHorizSq = 1.0f - (Rg * Rg) / (r * r);
    const float cosHoriz = -sqrt(max(0.0f, sinHorizSq));

    // Spherical local coordinate frame at r
    const float sinMuS = sqrt(max(0.0f, 1.0f - mu_s * mu_s));
    const float3 sDir = float3(sinMuS, mu_s, 0.0f);

    const float vX = (sinMuS > 1e-4f) ? ((nu - mu * mu_s) / sinMuS) : 0.0f;
    const float vY = mu;
    const float vZ = sqrt(max(0.0f, 1.0f - vX * vX - vY * vY));
    const float3 vDir = float3(vX, vY, vZ);

    // Numerical integration of Delta J over 32 spherical directions
    const uint NUM_SPHERE_SAMPLES = 32u;
    const float dOmega = (4.0f * ATMO_PI) / float(NUM_SPHERE_SAMPLES);
    float3 deltaJ = float3(0.0f, 0.0f, 0.0f);

    for (uint i = 0u; i < NUM_SPHERE_SAMPLES; ++i)
    {
        const float y_w = 1.0f - (float(i) + 0.5f) * (2.0f / float(NUM_SPHERE_SAMPLES));
        const float phi_w = float(i) * 2.399963229728653f;
        const float r_w = sqrt(max(0.0f, 1.0f - y_w * y_w));
        const float3 wDir = float3(cos(phi_w) * r_w, y_w, sin(phi_w) * r_w);

        const float mu_w = wDir.y;
        const float nu_w = dot(vDir, wDir);
        const float mu_s_w = dot(sDir, wDir);

        float3 L_in = float3(0.0f, 0.0f, 0.0f);
        if (mu_w < cosHoriz)
        {
            // Direction intersects planet ground
            const float d_ground = -r * mu_w - sqrt(max(0.0f, r * r * (mu_w * mu_w - 1.0f) + Rg * Rg));
            const float3 groundPos = float3(0.0f, r, 0.0f) + d_ground * wDir;
            const float mu_s_ground = dot(normalize(groundPos), sDir);

            const float mu_w_ground = (r * mu_w + d_ground) / Rg;
            const float3 T_ground = clamp(SampleTransmittanceLut(TransmittanceLut, AtmosphereLinearSampler, params, Rg, -mu_w_ground) /
                                          max(float3(1e-4f), SampleTransmittanceLut(TransmittanceLut, AtmosphereLinearSampler, params, r, -mu_w)),
                                          float3(0.0f), float3(1.0f));
            const float3 deltaE_ground = SampleIrradianceLut(DeltaIrradianceLut, AtmosphereLinearSampler, params, Rg, mu_s_ground);
            L_in = T_ground * (params.GroundAlbedo / ATMO_PI) * deltaE_ground;
        }
        else
        {
            // Direction points towards sky
            if (params.ScatteringOrder == 2u)
            {
                const float4 rayleighIn = SampleInscatterLut4D(DeltaRayleighLut, AtmosphereLinearSampler, params, r, mu_w, mu_s_w, nu_w);
                const float4 mieIn = SampleInscatterLut4D(DeltaMieLut, AtmosphereLinearSampler, params, r, mu_w, mu_s_w, nu_w);
                L_in = rayleighIn.rgb * PhaseFunctionRayleigh(nu_w) + mieIn.rgb * PhaseFunctionMie(nu_w, params.MieAnisotropy);
            }
            else
            {
                const float4 multiIn = SampleInscatterLut4D(DeltaMultipleScatteringLut, AtmosphereLinearSampler, params, r, mu_w, mu_s_w, nu_w);
                L_in = multiIn.rgb;
            }
        }

        const float h = r - Rg;
        const float rho_R = exp(-h / params.RayleighScaleHeightKm);
        const float rho_M = exp(-h / params.MieScaleHeightKm);

        deltaJ += (params.RayleighScattering * rho_R * PhaseFunctionRayleigh(nu_w) +
                   params.MieScattering * rho_M * PhaseFunctionMie(nu_w, params.MieAnisotropy)) * L_in * dOmega;
    }

    DeltaScatteringDensityLutWrite[id] = float4(deltaJ, 0.0f);
}

// -------------------------------------------------------------------------------------------------
// 5. AtmosphereIndirectIrradianceCS
// Computes indirect sky irradiance Delta E from previous order scattering.
// -------------------------------------------------------------------------------------------------

[shader("compute")]
[numthreads(8, 8, 1)]
void AtmosphereIndirectIrradianceCS(
    uint3 id : SV_DispatchThreadID,
    uniform AtmosphereParameters params)
{
    if (id.x >= 64u || id.y >= 16u)
    {
        return;
    }

    const float2 uv = (float2(id.xy) + 0.5f) / float2(64.0f, 16.0f);
    float r, mu_s;
    GetRadiusAndMuSFromIrradianceUV(params, uv, r, mu_s);

    const float sinMuS = sqrt(max(0.0f, 1.0f - mu_s * mu_s));
    const float3 sDir = float3(sinMuS, mu_s, 0.0f);

    // Hemispherical numerical integration over upper hemisphere (cos(theta) >= 0)
    const uint NUM_HEMI_SAMPLES = 32u;
    const float dOmega = (2.0f * ATMO_PI) / float(NUM_HEMI_SAMPLES);
    float3 deltaE = float3(0.0f, 0.0f, 0.0f);

    for (uint i = 0u; i < NUM_HEMI_SAMPLES; ++i)
    {
        const float cosTheta = (float(i) + 0.5f) / float(NUM_HEMI_SAMPLES);
        const float phi = float(i) * 2.399963229728653f;
        const float sinTheta = sqrt(max(0.0f, 1.0f - cosTheta * cosTheta));
        const float3 wDir = float3(cos(phi) * sinTheta, cosTheta, sin(phi) * sinTheta);

        const float nu_w = dot(sDir, wDir);
        float3 S_in = float3(0.0f, 0.0f, 0.0f);

        if (params.ScatteringOrder == 2u)
        {
            const float4 rayleighIn = SampleInscatterLut4D(DeltaRayleighLut, AtmosphereLinearSampler, params, r, cosTheta, mu_s, nu_w);
            const float4 mieIn = SampleInscatterLut4D(DeltaMieLut, AtmosphereLinearSampler, params, r, cosTheta, mu_s, nu_w);
            S_in = rayleighIn.rgb * PhaseFunctionRayleigh(nu_w) + mieIn.rgb * PhaseFunctionMie(nu_w, params.MieAnisotropy);
        }
        else
        {
            const float4 multiIn = SampleInscatterLut4D(DeltaMultipleScatteringLut, AtmosphereLinearSampler, params, r, cosTheta, mu_s, nu_w);
            S_in = multiIn.rgb;
        }

        deltaE += S_in * cosTheta * dOmega;
    }

    DeltaIrradianceLutWrite[id.xy] = float4(deltaE, 0.0f);
    IrradianceLutWrite[id.xy] += float4(deltaE, 0.0f);
}

// -------------------------------------------------------------------------------------------------
// 6. AtmosphereMultipleScatteringCS
// Computes higher-order inscattering Delta S along view rays and accumulates into InscatterLutWrite.
// -------------------------------------------------------------------------------------------------

[shader("compute")]
[numthreads(8, 8, 4)]
void AtmosphereMultipleScatteringCS(
    uint3 id : SV_DispatchThreadID,
    uniform AtmosphereParameters params)
{
    if (id.x >= 256u || id.y >= 128u || id.z >= 32u)
    {
        return;
    }

    float r, mu, mu_s, nu, d_ray;
    GetPhysicalFromInscatterTexel(params, id, r, mu, mu_s, nu, d_ray);

    const float Rg = params.BottomRadiusKm;
    const uint NUM_STEPS = 50u;
    const float ds = d_ray / float(NUM_STEPS);
    float3 deltaS = float3(0.0f, 0.0f, 0.0f);

    for (uint i = 0u; i < NUM_STEPS; ++i)
    {
        const float s = (float(i) + 0.5f) * ds;
        const float r_s = sqrt(max(Rg * Rg, r * r + 2.0f * r * s * mu + s * s));
        const float mu_v_s = (r * mu + s) / max(1e-4f, r_s);
        const float mu_s_s = (r * mu_s + s * nu) / max(1e-4f, r_s);

        float3 T_view;
        const float mu_horiz_r = -sqrt(max(0.0f, 1.0f - (Rg * Rg) / (r * r)));
        if (mu < mu_horiz_r)
        {
            const float3 T_samp = SampleTransmittanceLut(TransmittanceLut, AtmosphereLinearSampler, params, r_s, -mu_v_s);
            const float3 T_cam = SampleTransmittanceLut(TransmittanceLut, AtmosphereLinearSampler, params, r, -mu);
            T_view = clamp(T_samp / max(float3(1e-4f), T_cam), float3(0.0f), float3(1.0f));
        }
        else
        {
            const float3 T_cam = SampleTransmittanceLut(TransmittanceLut, AtmosphereLinearSampler, params, r, mu);
            const float3 T_samp = SampleTransmittanceLut(TransmittanceLut, AtmosphereLinearSampler, params, r_s, mu_v_s);
            T_view = clamp(T_cam / max(float3(1e-4f), T_samp), float3(0.0f), float3(1.0f));
        }

        const float4 deltaJ = SampleInscatterLut4D(DeltaScatteringDensityLut, AtmosphereLinearSampler, params, r_s, mu_v_s, mu_s_s, nu);
        deltaS += T_view * deltaJ.rgb * ds;
    }

    DeltaMultipleScatteringLutWrite[id] = float4(deltaS, 0.0f);

    // Accumulate unweighted multiple scattering radiance into C_* (RGB channels of InscatterLutWrite)
    const float phaseR = PhaseFunctionRayleigh(nu);
    InscatterLutWrite[id] += float4(deltaS / max(1e-4f, phaseR), 0.0f);
}

// -------------------------------------------------------------------------------------------------
// Clean Public Runtime Evaluation Functions (converting Rec.709 to AP1 linear space)
// -------------------------------------------------------------------------------------------------

// Evaluates spectral transmittance between camera position and top of atmosphere along viewDir
[ForceInline]
public float3 EvaluateAtmosphereTransmittance(
    Texture2D<float4> transmittanceLut,
    SamplerState atmoSampler,
    AtmosphereParameters params,
    float3 cameraPosKm,
    float3 viewDir)
{
    const float r = length(cameraPosKm);
    const float mu = dot(cameraPosKm, viewDir) / max(1e-4f, r);
    return SampleTransmittanceLut(transmittanceLut, atmoSampler, params, r, mu);
}

// Evaluates sky radiance along view ray in AP1 linear space
[ForceInline]
public float3 EvaluateAtmosphereSkyRadiance(
    Texture3D<float4> inscatterLut,
    Texture2D<float4> transmittanceLut,
    SamplerState atmoSampler,
    AtmosphereParameters params,
    float3 cameraPosKm,
    float3 viewDir,
    float3 sunDir)
{
    const float r = length(cameraPosKm);
    const float mu = dot(cameraPosKm, viewDir) / max(1e-4f, r);
    const float mu_s = dot(cameraPosKm, sunDir) / max(1e-4f, r);
    const float nu = dot(viewDir, sunDir);

    const float4 inscatter = SampleInscatterLut4D(inscatterLut, atmoSampler, params, r, mu, mu_s, nu);
    const float3 C_star = inscatter.rgb;
    const float C_M_r = inscatter.a;

    // Reconstruct full RGB Mie scattering vector via spectral proportionality
    const float3 betaR = params.RayleighScattering;
    const float3 betaM = params.MieScattering;
    const float ratioFactor = (C_M_r / max(1e-6f, C_star.r)) * (betaR.r / max(1e-6f, betaM.r));
    const float3 C_M = C_star * ratioFactor * (betaM / max(float3(1e-6f, 1e-6f, 1e-6f), betaR));

    float3 radianceRec709 = PhaseFunctionRayleigh(nu) * C_star + PhaseFunctionMie(nu, params.MieAnisotropy) * C_M;

    // Add direct solar disk if viewing ray points directly into sun
    static const float k_CosSolarRadius = 0.99996f; // ~0.267 degrees solar disk
    if (nu >= k_CosSolarRadius)
    {
        const float3 T_sun = SampleTransmittanceLut(transmittanceLut, atmoSampler, params, r, mu_s);
        radianceRec709 += T_sun * (params.SolarIrradiance / (ATMO_PI * (1.0f - k_CosSolarRadius)));
    }

    return Rec709ToAp1(radianceRec709);
}

// Evaluates aerial perspective along ray segment [cameraPosKm, targetPosKm] in AP1 linear space
[ForceInline]
public AerialPerspectiveResult EvaluateAtmosphereAerialPerspective(
    Texture3D<float4> inscatterLut,
    Texture2D<float4> transmittanceLut,
    SamplerState atmoSampler,
    AtmosphereParameters params,
    float3 cameraPosKm,
    float3 targetPosKm,
    float3 sunDir)
{
    const float3 segment = targetPosKm - cameraPosKm;
    const float dist = length(segment);
    const float3 vDir = (dist > 1e-4f) ? (segment / dist) : float3(0.0f, 1.0f, 0.0f);

    const float r_cam = length(cameraPosKm);
    const float mu_cam = dot(cameraPosKm, vDir) / max(1e-4f, r_cam);
    const float mu_s_cam = dot(cameraPosKm, sunDir) / max(1e-4f, r_cam);
    const float nu = dot(vDir, sunDir);

    const float r_target = length(targetPosKm);
    const float mu_target = dot(targetPosKm, vDir) / max(1e-4f, r_target);
    const float mu_s_target = dot(targetPosKm, sunDir) / max(1e-4f, r_target);

    float3 segmentTransmittance;
    if (mu_cam < 0.0f)
    {
        const float3 T_target = SampleTransmittanceLut(transmittanceLut, atmoSampler, params, r_target, -mu_target);
        const float3 T_cam = SampleTransmittanceLut(transmittanceLut, atmoSampler, params, r_cam, -mu_cam);
        segmentTransmittance = clamp(T_target / max(float3(1e-4f), T_cam), float3(0.0f), float3(1.0f));
    }
    else
    {
        const float3 T_cam = SampleTransmittanceLut(transmittanceLut, atmoSampler, params, r_cam, mu_cam);
        const float3 T_target = SampleTransmittanceLut(transmittanceLut, atmoSampler, params, r_target, mu_target);
        segmentTransmittance = clamp(T_cam / max(float3(1e-4f), T_target), float3(0.0f), float3(1.0f));
    }

    // Difference of precomputed integrals: S(x, x_0) = S(x, v) - T(x, x_0) * S(x_0, v)
    const float4 s_cam = SampleInscatterLut4D(inscatterLut, atmoSampler, params, r_cam, mu_cam, mu_s_cam, nu);
    const float4 s_target = SampleInscatterLut4D(inscatterLut, atmoSampler, params, r_target, mu_target, mu_s_target, nu);

    const float3 inscatterRec709 = max(float3(0.0f, 0.0f, 0.0f), s_cam.rgb - segmentTransmittance * s_target.rgb);

    AerialPerspectiveResult result;
    result.Transmittance = segmentTransmittance;
    result.InscatterAP1 = Rec709ToAp1(inscatterRec709);
    return result;
}

// Evaluates direct + diffuse solar irradiance on a horizontal surface in AP1 linear space
[ForceInline]
public float3 EvaluateAtmosphereSunIrradiance(
    Texture2D<float4> irradianceLut,
    Texture2D<float4> transmittanceLut,
    SamplerState atmoSampler,
    AtmosphereParameters params,
    float3 positionKm,
    float3 sunDir)
{
    const float r = length(positionKm);
    const float mu_s = dot(positionKm, sunDir) / max(1e-4f, r);

    const float3 directT = SampleTransmittanceLut(transmittanceLut, atmoSampler, params, r, mu_s);
    const float3 directE = params.SolarIrradiance * directT * max(0.0f, mu_s);
    const float3 diffuseE = SampleIrradianceLut(irradianceLut, atmoSampler, params, r, mu_s);

    const float3 totalIrradianceRec709 = directE + diffuseE;
    return Rec709ToAp1(totalIrradianceRec709);
}
