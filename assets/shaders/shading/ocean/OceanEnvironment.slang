module OceanEnvironment;
import LodestoneAttributes;
import LodestoneShims;

// OceanEnvironment.slang
// Synthesizes equirectangular skydome environments (Analytic Preetham and HDRIs),
// generates full mip chains via box-downsampling compute passes, and renders
// a fullscreen sky background triangle into the ocean scene in AP1 linear space.

internal static const float ENV_PI = 3.14159265358979323846f;

// Bradford-adapted CIE XYZ (D65/ACES) to AP1 linear matrix
internal static const float3x3 XYZ_TO_AP1_MATRIX = float3x3(
    1.64102338f, -0.663662859f, 0.0117218943f,
    -0.324803294f, 1.61533159f, -0.008284442f,
    -0.236424695f, 0.0167563477f, 0.988394859f);

// Parameters for Analytic Preetham skydome synthesis
public struct PreethamPushConstants
{
    public uint2 Dimensions;
    public float Turbidity;
    public float Exposure;
    public float3 SunDirection;
    public float SolarDiskRadius;
    public float SolarDiskIntensity;
    public float3 Pad;
};

// Parameters for GPU box-downsampling of equirectangular mips
public struct DownsamplePushConstants
{
    public uint2 DstDimensions;
    public uint2 SrcDimensions;
};

// Parameters for fullscreen sky background rendering
public struct SkyPushConstants
{
    public float4 InvViewProjRow0;
    public float4 InvViewProjRow1;
    public float4 InvViewProjRow2;
    public float4 InvViewProjRow3;
    public float4 CameraPosition;
    public float Exposure;
    public float Lod;
    public float2 Pad;
};

// Perez sky luminance and chromaticity distribution coefficients
internal struct PerezCoefficients
{
    float A;
    float B;
    float C;
    float D;
    float E;
};

internal float EvaluatePerez(PerezCoefficients c, float cosTheta, float gamma, float cosGamma)
{
    const float num1 = 1.0f + c.A * exp(c.B / max(0.01f, cosTheta));
    const float num2 = 1.0f + c.C * exp(c.D * gamma) + c.E * cosGamma * cosGamma;
    return num1 * num2;
}

[format("rgba16f")]
[ls_extent_2d("8192", "4096")]
RWTexture2D<float4> OutPreethamMip0;

[shader("compute")]
[numthreads(8, 8, 1)]
void OceanEnvPreethamCS(uint3 id : SV_DispatchThreadID, uniform PreethamPushConstants params)
{
    if (id.x >= params.Dimensions.x || id.y >= params.Dimensions.y)
    {
        return;
    }

    // Convert pixel to spherical direction (equirectangular projection)
    // u in [0, 1] maps phi in [-PI, +PI]
    // v in [0, 1] maps theta in [0, PI] (0 = +Y zenith, 1 = -Y nadir)
    const float u = (float(id.x) + 0.5f) / float(params.Dimensions.x);
    const float v = (float(id.y) + 0.5f) / float(params.Dimensions.y);

    const float phi = (u - 0.5f) * 2.0f * ENV_PI;
    const float theta = v * ENV_PI;

    const float sinTheta = sin(theta);
    const float cosTheta = cos(theta);

    const float3 viewDir = float3(
        sinTheta * cos(phi),
        cosTheta,
        sinTheta * sin(phi)
    );

    const float3 sunDir = normalize(params.SunDirection);
    const float cosSunZenith = clamp(sunDir.y, 0.001f, 1.0f);
    const float thetaS = acos(cosSunZenith);

    const float turbidity = clamp(params.Turbidity, 1.0f, 10.0f);
    const float turbiditySq = turbidity * turbidity;

    // Perez distribution coefficients for Y, x, y
    PerezCoefficients cy;
    cy.A = 0.17872f * turbidity - 1.46303f;
    cy.B = -0.35540f * turbidity + 0.42749f;
    cy.C = -0.02266f * turbidity + 5.32505f;
    cy.D = 0.12064f * turbidity - 2.57705f;
    cy.E = -0.06696f * turbidity + 0.37027f;

    PerezCoefficients cx;
    cx.A = -0.01925f * turbidity - 0.25922f;
    cx.B = -0.06651f * turbidity + 0.00081f;
    cx.C = -0.00041f * turbidity + 0.21247f;
    cx.D = -0.06409f * turbidity - 0.89887f;
    cx.E = -0.00325f * turbidity + 0.04517f;

    PerezCoefficients cz;
    cz.A = -0.01669f * turbidity - 0.26078f;
    cz.B = -0.08864f * turbidity + 0.00921f;
    cz.C = -0.00516f * turbidity + 0.21023f;
    cz.D = -0.06029f * turbidity - 1.65369f;
    cz.E = -0.00373f * turbidity + 0.05291f;

    // Zenith luminance Yz (in cd/m^2)
    const float chi = (4.0f / 9.0f - turbidity / 120.0f) * (ENV_PI - 2.0f * thetaS);
    float yZ = (4.0453f * turbidity - 4.9710f) * tan(chi) - 0.2155f * turbidity + 2.4192f;
    yZ = max(0.0f, yZ) * 1000.0f;

    // Zenith chromaticities xz, yz
    const float thetaS2 = thetaS * thetaS;
    const float thetaS3 = thetaS2 * thetaS;

    const float xZ = (0.00166f * turbiditySq - 0.02903f * turbidity + 0.11693f) * thetaS3
                   + (-0.00375f * turbiditySq + 0.06377f * turbidity - 0.21196f) * thetaS2
                   + (0.00209f * turbiditySq - 0.03202f * turbidity + 0.06052f) * thetaS
                   + (0.00394f * turbidity + 0.25886f);

    const float yZChrom = (0.00275f * turbiditySq - 0.04214f * turbidity + 0.15346f) * thetaS3
                        + (-0.00610f * turbiditySq + 0.08970f * turbidity - 0.26756f) * thetaS2
                        + (0.00317f * turbiditySq - 0.04517f * turbidity + 0.06670f) * thetaS
                        + (0.00516f * turbidity + 0.26688f);

    // View zenith and sun angle
    const float cosView = max(0.001f, viewDir.y);
    const float cosGamma = clamp(dot(viewDir, sunDir), -1.0f, 1.0f);
    const float gamma = acos(cosGamma);

    // Perez evaluations
    const float fY = EvaluatePerez(cy, cosView, gamma, cosGamma) / max(1e-6f, EvaluatePerez(cy, 1.0f, thetaS, cosSunZenith));
    const float fX = EvaluatePerez(cx, cosView, gamma, cosGamma) / max(1e-6f, EvaluatePerez(cx, 1.0f, thetaS, cosSunZenith));
    const float fZ = EvaluatePerez(cz, cosView, gamma, cosGamma) / max(1e-6f, EvaluatePerez(cz, 1.0f, thetaS, cosSunZenith));

    const float lumY = yZ * fY;
    const float chromX = xZ * fX;
    const float chromY = max(1e-4f, yZChrom * fZ);

    // Convert xyY to CIE XYZ
    const float cieX = (chromX / chromY) * lumY;
    const float cieZ = max(0.0f, (1.0f - chromX - chromY) / chromY * lumY);

    // Convert XYZ to AP1 linear RGB
    float3 ap1 = mul(float3(cieX, lumY, cieZ), XYZ_TO_AP1_MATRIX);
    ap1 = max(float3(0.0f), ap1);

    // Solar disk with smooth limb darkening
    const float diskRadius = max(0.00465f, params.SolarDiskRadius);
    const float cosDisk = cos(diskRadius);
    if (cosGamma > cosDisk)
    {
        const float limb = saturate((cosGamma - cosDisk) / max(1e-6f, 1.0f - cosDisk));
        const float diskRadiance = (params.SolarDiskIntensity > 0.0f ? params.SolarDiskIntensity : 20000.0f) * (0.4f + 0.6f * limb);
        const float3 sunColor = float3(1.0f, 0.95f, 0.88f);
        ap1 += sunColor * diskRadiance;
    }

    // Smooth ground extinction below horizon
    if (viewDir.y < 0.0f)
    {
        const float groundFade = exp(viewDir.y * 8.0f);
        ap1 *= 0.15f * groundFade;
    }

    const float exposure = max(1e-4f, params.Exposure);
    OutPreethamMip0[id.xy] = float4(ap1 * exposure, 1.0f);
}

[ls_extent_2d("8192", "4096")]
Texture2D<float4> SrcMip;
[format("rgba16f")]
[ls_extent_2d("4096", "2048")]
RWTexture2D<float4> DstMip;

[shader("compute")]
[numthreads(8, 8, 1)]
void OceanEnvDownsampleMipCS(uint3 id : SV_DispatchThreadID, uniform DownsamplePushConstants push)
{
    if (id.x >= push.DstDimensions.x || id.y >= push.DstDimensions.y)
    {
        return;
    }

    const uint2 src00 = id.xy * 2u;
    const uint2 src10 = min(src00 + uint2(1u, 0u), push.SrcDimensions - uint2(1u, 1u));
    const uint2 src01 = min(src00 + uint2(0u, 1u), push.SrcDimensions - uint2(1u, 1u));
    const uint2 src11 = min(src00 + uint2(1u, 1u), push.SrcDimensions - uint2(1u, 1u));

    const float4 c00 = SrcMip.Load(int3(src00, 0));
    const float4 c10 = SrcMip.Load(int3(src10, 0));
    const float4 c01 = SrcMip.Load(int3(src01, 0));
    const float4 c11 = SrcMip.Load(int3(src11, 0));

    DstMip[id.xy] = 0.25f * (c00 + c10 + c01 + c11);
}

public struct OceanSkyVaryings
{
    public float4 Position : SV_Position;
    public float3 WorldViewRay : WORLD_VIEW_RAY;
};

[shader("vertex")]
OceanSkyVaryings OceanSkyVS(uint vertexId : SV_VertexID, uniform SkyPushConstants sky)
{
    const float2 corner = float2(float((vertexId << 1) & 2), float(vertexId & 2));
    const float2 ndc = corner * 2.0f - 1.0f;
    const float4 clipPos = float4(ndc, 0.0f, 1.0f);

    const float4 worldH = float4(
        dot(sky.InvViewProjRow0, clipPos),
        dot(sky.InvViewProjRow1, clipPos),
        dot(sky.InvViewProjRow2, clipPos),
        dot(sky.InvViewProjRow3, clipPos)
    );

    OceanSkyVaryings output;
    output.Position = float4(ndc, 0.0f, 1.0f);
    const float3 worldPos = (abs(worldH.w) > 1e-7f) ? (worldH.xyz / worldH.w) : worldH.xyz;
    output.WorldViewRay = worldPos - sky.CameraPosition.xyz;
    return output;
}

[ls_extent_2d("8192", "4096")]
Texture2D<float4> SkyDomeTexture;
SamplerState SkyDomeSampler;

[ForceInline]
internal float FastAcos(float x)
{
    return acos(clamp(x, -1.0f, 1.0f));
}

[ForceInline]
internal float3 SampleEquirectangularSky(
    Texture2D<float4> skyTex,
    SamplerState skySampler,
    float3 dir)
{
    const float phi = atan2(dir.z, dir.x);
    const float theta = acos(clamp(dir.y, -1.0f, 1.0f));
    const float2 uv = float2(phi * (0.5f / ENV_PI) + 0.5f, theta * (1.0f / ENV_PI));
    return skyTex.Sample(skySampler, uv).rgb;
}

[ForceInline]
internal float3 SampleEquirectangularSkyLevel(
    Texture2D<float4> skyTex,
    SamplerState skySampler,
    float3 dir,
    float lod)
{
    const float phi = atan2(dir.z, dir.x);
    const float theta = acos(clamp(dir.y, -1.0f, 1.0f));
    const float2 uv = float2(phi * (0.5f / ENV_PI) + 0.5f, theta * (1.0f / ENV_PI));
    return skyTex.SampleLevel(skySampler, uv, lod).rgb;
}

[ForceInline]
internal float3 SampleEquirectangularSkyGrad(
    Texture2D<float4> skyTex,
    SamplerState skySampler,
    float3 dir,
    float2 ddxUv,
    float2 ddyUv,
    float roughnessBias)
{
    const float phi = atan2(dir.z, dir.x);
    const float theta = acos(clamp(dir.y, -1.0f, 1.0f));
    const float2 uv = float2(phi * (0.5f / ENV_PI) + 0.5f, theta * (1.0f / ENV_PI));

    float2 ddx = ddxUv;
    ddx.x -= round(ddx.x);
    float2 ddy = ddyUv;
    ddy.x -= round(ddy.x);

    const float maxGrad = 0.25f;
    ddx = clamp(ddx, float2(-maxGrad), float2(maxGrad));
    ddy = clamp(ddy, float2(-maxGrad), float2(maxGrad));

    const float gradScale = exp2(roughnessBias);
    ddx *= gradScale;
    ddy *= gradScale;

    return skyTex.SampleGrad(skySampler, uv, ddx, ddy).rgb;
}

[shader("fragment")]
float4 OceanSkyFS(OceanSkyVaryings input, uniform SkyPushConstants sky) : SV_Target
{
    const float3 ray = normalize(input.WorldViewRay);
    const float3 sampled = SampleEquirectangularSkyLevel(SkyDomeTexture, SkyDomeSampler, ray, sky.Lod);
    return float4(sampled * sky.Exposure, 1.0f);
}
