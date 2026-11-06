module OceanDeferred;

import LodestoneAttributes;
import LodestoneShims;
import OceanAtmosphere;
import ddColor;
import ddEon;

// OceanDeferred.slang
// Deferred ocean rendering post-passes:
// 1. OceanSpecularAaCS: Toksvig / Kaplanyan (2016) Screen-Space Specular Normal Variance AA.
// 2. OceanHiZDownsampleCS: Reverse-Z hierarchical depth downsampling.
// 3. OceanDeferredLightingCS: Deferred shading, dual-depth refraction, SSR, and aerial perspective.
// 4. OceanTaaResolveCS: Variance-clipped Karis-weighted Temporal Antialiasing.
// 5. OceanAutoExposureCS: Single-pass shared-memory log2 luminance reduction and eye adaptation.

internal static const float DEFERRED_INV_PI = 0.31830988618379067154f;

public struct OceanViewParameters
{
    public float4 ViewProjectionRows[4];
    public float4 CameraPositionTime;
    public float4 LengthScales[2];
    public float4 CascadeWeights[2];
    public float4 FoamWeights[2];
    public float4 SunDirectionSeabedDepth;
    public float4 LodParameters;
    public float4 WhitecapParameters;
    public float4 PersistentFoamParameters;
    public float4 FoamDrift;
    public float4 FoamResidualParameters;
    public float4 ShadingParameters;
    public float4 FoamTextureParameters;
    public float4 FoamTextureTint;
};

public struct FrameConstants
{
    public float4 SkyShCoeffs[9];
    public float3 SunRadianceAp1;
    public float FoamRoughnessScale;
    public float3 WaterScatterColorAp1;
    public float SssStrength;
    public float3 SeabedTintAp1;
    public float SeabedRoughnessScale;
    public uint UseCdlod;
    public uint UseSkyTexture;
    public float SkyIntensityScale;
    public float LeadrSlopeVarianceScale;
    public float4 PrevViewProjectionRows[4];
    public float2 JitterOffsetNdc;
    public float2 PrevJitterOffsetNdc;
    public uint UseBrunetonAtmosphere;
    public uint UseDeferredPath;
    public uint UseSpecularAa;
    public uint UseTaa;
    public float SpecularAaStrength;
    public float SsrIntensity;
    public float TaaBlendAlpha;
    public float AutoExposureSpeed;
    public float3 WaterLeavingReflectanceAp1;
    public float BodyReflectanceScale;
    public property float3 FoamAlbedoTint
    {
        get
        {
            return View.FoamTextureTint.xyz;
        }
    }
};

public ConstantBuffer<OceanViewParameters> View;
public ConstantBuffer<FrameConstants> Frame;
public ConstantBuffer<AtmosphereParameters> Atmosphere;

// G-Buffer resources
[ls_extent_2d("1920", "1080")]
public Texture2D<float4> GBufferNormalSigmaIn;

[format("rgba16f")]
[ls_extent_2d("1920", "1080")]
public RWTexture2D<float4> GBufferNormalSigmaOut;

[ls_extent_2d("1920", "1080")]
public Texture2D<float4> GBufferNormalSigmaFiltered;

[ls_extent_2d("1920", "1080")]
public Texture2D<float4> GBufferFoamSss;

[ls_extent_2d("1920", "1080")]
public Texture2D<float4> GBufferMotionFoam;

// Scene depth and seabed resources
[ls_extent_2d("1920", "1080")]
public Texture2D<float> OceanDepthTexture;

[ls_extent_2d("1920", "1080")]
public Texture2D<float4> SeabedColorTexture;

[ls_extent_2d("1920", "1080")]
public Texture2D<float> SeabedDepthTexture;

// Environment and reflection resources
[ls_extent_2d("8192", "4096")]
public Texture2D<float4> SkyDomeTexture;
public SamplerState SkyDomeSampler;

[ls_extent_2d("1920", "1080")]
public Texture2D<float4> HistoryColorTexture;
public SamplerState LinearClampSampler;
public SamplerState PointClampSampler;

// Bruneton atmosphere precomputed LUTs
[ls_extent_2d("256", "64")]
public Texture2D<float4> TransmittanceLut;

[ls_extent_2d("64", "16")]
public Texture2D<float4> IrradianceLut;

[ls_extent_3d("256", "128", "32")]
public Texture3D<float4> InscatterLut;
public SamplerState AtmosphereLinearSampler;

// Hi-Z resources
[ls_extent_2d("1920", "1080")]
public Texture2D<float> SrcDepth;

[format("r32f")]
[ls_extent_2d("960", "540")]
public RWTexture2D<float> DstHiZMip;

// HDR Lighting and TAA resources
[format("rgba16f")]
[ls_extent_2d("1920", "1080")]
public RWTexture2D<float4> HdrLightingOutput;

[ls_extent_2d("1920", "1080")]
public Texture2D<float4> HdrLightingInput;

[format("rgba16f")]
[ls_extent_2d("1920", "1080")]
public RWTexture2D<float4> HdrResolvedOutput;

[format("rgba16f")]
[ls_extent_2d("1920", "1080")]
public RWTexture2D<float4> NextHistoryOutput;

// Auto-exposure resources
[ls_extent_2d("1920", "1080")]
public Texture2D<float4> HdrResolvedInput;

[ls_element_count("1")]
public RWStructuredBuffer<float4> ExposureStateBuffer;

internal groupshared float s_SharedLogLum[256];

// Push constant definitions
public struct SpecularAaPushConstants
{
    public uint2 Dimensions;
    public float SpecularAaStrength;
    public float Padding;
};

public struct HiZPushConstants
{
    public uint2 DstDimensions;
    public uint2 SrcDimensions;
};

public struct DeferredLightingPushConstants
{
    public float4 InvViewProjection[4];
    public uint2 Dimensions;
    public float2 InvDimensions;
};

public struct TaaPushConstants
{
    public uint2 Dimensions;
    public float2 InvDimensions;
};

public struct AutoExposurePushConstants
{
    public uint2 Dimensions;
    public float DeltaTime;
    public float AdaptationSpeed;
    public float MinLogLum;
    public float MaxLogLum;
    public float2 Padding;
};

// -------------------------------------------------------------------------------------------------
// Helper Functions
// -------------------------------------------------------------------------------------------------

internal float3 ClipToWorld(float4 clip, float4 invVp[4])
{
    const float4 worldH = float4(
        dot(invVp[0], clip),
        dot(invVp[1], clip),
        dot(invVp[2], clip),
        dot(invVp[3], clip)
    );
    return worldH.xyz / max(1e-7f, worldH.w);
}

internal float Erfc(float x)
{
    return 2.0f * exp(-x * x) / (2.319f * x + sqrt(4.0f + 1.52f * x * x));
}

internal float Lambda(float cosTheta, float sigmaSq)
{
    const float v = cosTheta / sqrt(max(1e-5f, (1.0f - cosTheta * cosTheta) * (2.0f * sigmaSq)));
    return max(0.0f, (exp(-v * v) - v * 1.77245385f * Erfc(v)) / max(1e-5f, 2.0f * v * 1.77245385f));
}

internal float EvaluateOceanSpecular(
    float3 L,
    float3 V,
    float3 N,
    float3 Tx,
    float3 Ty,
    float2 sigmaSq)
{
    const float3 H = normalize(L + V);
    const float zH = max(1e-4f, dot(H, N));
    const float zetax = dot(H, Tx) / zH;
    const float zetay = dot(H, Ty) / zH;
    const float zL = max(0.001f, dot(L, N));
    const float zV = max(0.001f, dot(V, N));
    const float zH2 = zH * zH;

    const float p = exp(-0.5f * (zetax * zetax / sigmaSq.x + zetay * zetay / sigmaSq.y))
        / (2.0f * 3.14159265f * sqrt(sigmaSq.x * sigmaSq.y));

    const float vx = dot(V, Tx);
    const float vy = dot(V, Ty);
    const float vLenSq = vx * vx + vy * vy;
    const float cosV2 = (vLenSq > 1e-6f) ? (vx * vx / vLenSq) : 1.0f;
    const float sigmaV2 = sigmaSq.x * cosV2 + sigmaSq.y * (1.0f - cosV2);

    const float lx = dot(L, Tx);
    const float ly = dot(L, Ty);
    const float lLenSq = lx * lx + ly * ly;
    const float cosL2 = (lLenSq > 1e-6f) ? (lx * lx / lLenSq) : 1.0f;
    const float sigmaL2 = sigmaSq.x * cosL2 + sigmaSq.y * (1.0f - cosL2);

    const float tVh = 1.0f - saturate(dot(V, H));
    const float tVh2 = tVh * tVh;
    const float tVh5 = tVh2 * tVh2 * tVh;
    const float fresnel = 0.02f + 0.98f * tVh5;
    const float denom = (1.0f + Lambda(zL, sigmaL2) + Lambda(zV, sigmaV2)) * zV * zH2 * zH2 * 4.0f;

    return (fresnel * p) / max(1e-5f, denom);
}

internal float EvaluateBrunetonMeanFresnel(float cosThetaV, float sigmaV)
{
    const float exponent = 5.0f * exp(-2.69f * sigmaV);
    const float denom = 1.0f + 22.7f * pow(max(0.0f, sigmaV), 1.5f);
    return pow(max(0.0f, 1.0f - cosThetaV), exponent) / max(1e-5f, denom);
}

internal float EvaluateBrunetonFresnel(float3 V, float3 N, float2 sigmaSq)
{
    const float sigmaV = sqrt(max(0.0f, 0.5f * (sigmaSq.x + sigmaSq.y)));
    const float mu = dot(V, N);
    const float muMin = clamp(0.08f + 0.30f * sigmaV, 0.08f, 0.22f);
    const float cosThetaV = saturate(0.5f * (mu + sqrt(mu * mu + muMin * muMin)));
    return 0.02f + 0.98f * EvaluateBrunetonMeanFresnel(cosThetaV, sigmaV);
}

// Smoothly limits away-facing wave back-slopes (nv < 0) to the Smith visible-normal masking bound so back-slopes
// reflect the ambient mid-sky (reflect(-V, N).y >= 0.55 * V.y) instead of clamping onto the 0-degree horizon band.
internal float3 RegularizeGrazingNormal(float3 V, float3 N, float2 sigmaSq)
{
    const float2 vHorizXz = V.xz;
    const float vHorizLen = length(vHorizXz);
    if (vHorizLen < 1e-4f)
    {
        return N;
    }

    const float2 vDirXz = vHorizXz / vHorizLen;
    const float nv = dot(N.xz, vDirXz);
    const float2 nPerpXz = N.xz - nv * vDirXz;

    const float sigmaV = sqrt(max(0.0f, 0.5f * (sigmaSq.x + sigmaSq.y)));
    const float effectiveVy = max(0.05f, V.y + 0.20f * sigmaV);
    const float nvMin = -0.18f * (effectiveVy / vHorizLen) * N.y;
    const float diff = nv - nvMin;
    const float smoothness = 0.08f * N.y;
    const float nvMasked = nvMin + 0.5f * (diff + sqrt(diff * diff + smoothness * smoothness));

    const float2 maskedXz = nPerpXz + nvMasked * vDirXz;
    return normalize(float3(maskedXz.x, max(0.25f, N.y), maskedXz.y));
}

internal float3 RgbToYCoCg(float3 c)
{
    return float3(
        0.25f * c.r + 0.5f * c.g + 0.25f * c.b,
        0.5f * c.r - 0.5f * c.b,
       -0.25f * c.r + 0.5f * c.g - 0.25f * c.b
    );
}

internal float3 YCoCgToRgb(float3 c)
{
    return float3(
        c.x + c.y - c.z,
        c.x + c.z,
        c.x - c.y - c.z
    );
}

// -------------------------------------------------------------------------------------------------
// 1. OceanSpecularAaCS
// Screen-Space Specular Normal Variance Antialiasing (Toksvig / Kaplanyan 2016).
// -------------------------------------------------------------------------------------------------

// dispatch ((Push.Dimensions.x + 7) / 8, (Push.Dimensions.y + 7) / 8, 1)
[shader("compute")]
[numthreads(8, 8, 1)]
void OceanSpecularAaCS(
    uint3 id : SV_DispatchThreadID,
    uniform SpecularAaPushConstants Push)
{
    if (id.x >= Push.Dimensions.x || id.y >= Push.Dimensions.y)
    {
        return;
    }

    const int2 coord = int2(id.xy);
    const float4 centerSample = GBufferNormalSigmaIn.Load(int3(coord, 0));

    if (Frame.UseSpecularAa == 0u)
    {
        GBufferNormalSigmaOut[coord] = centerSample;
        return;
    }

    const int2 xClamp = int2(max(0, coord.x - 1), min(int(Push.Dimensions.x) - 1, coord.x + 1));
    const int2 yClamp = int2(max(0, coord.y - 1), min(int(Push.Dimensions.y) - 1, coord.y + 1));

    const float4 leftSample = GBufferNormalSigmaIn.Load(int3(xClamp.x, coord.y, 0));
    const float4 rightSample = GBufferNormalSigmaIn.Load(int3(xClamp.y, coord.y, 0));
    const float4 downSample = GBufferNormalSigmaIn.Load(int3(coord.x, yClamp.x, 0));
    const float4 upSample = GBufferNormalSigmaIn.Load(int3(coord.x, yClamp.y, 0));

    const float2 dNx = 0.5f * (rightSample.xy - leftSample.xy);
    const float2 dNy = 0.5f * (upSample.xy - downSample.xy);

    const float strength = max(0.0f, Push.SpecularAaStrength);
    const float deltaSigmaX = strength * (dNx.x * dNx.x + dNy.x * dNy.x);
    const float deltaSigmaZ = strength * (dNx.y * dNx.y + dNy.y * dNy.y);

    const float2 filteredSigma = centerSample.zw + float2(deltaSigmaX, deltaSigmaZ);
    GBufferNormalSigmaOut[coord] = float4(centerSample.x, centerSample.y, filteredSigma.x, filteredSigma.y);
}

// -------------------------------------------------------------------------------------------------
// 2. OceanHiZDownsampleCS
// Hierarchical Reverse-Z conservative depth downsampling.
// -------------------------------------------------------------------------------------------------

// dispatch ((Push.DstDimensions.x + 7) / 8, (Push.DstDimensions.y + 7) / 8, 1)
[shader("compute")]
[numthreads(8, 8, 1)]
void OceanHiZDownsampleCS(
    uint3 id : SV_DispatchThreadID,
    uniform HiZPushConstants Push)
{
    if (id.x >= Push.DstDimensions.x || id.y >= Push.DstDimensions.y)
    {
        return;
    }

    const uint2 srcBase = id.xy * 2u;
    const uint maxX = Push.SrcDimensions.x - 1u;
    const uint maxY = Push.SrcDimensions.y - 1u;

    const float d00 = SrcDepth.Load(int3(min(srcBase.x + 0u, maxX), min(srcBase.y + 0u, maxY), 0));
    const float d10 = SrcDepth.Load(int3(min(srcBase.x + 1u, maxX), min(srcBase.y + 0u, maxY), 0));
    const float d01 = SrcDepth.Load(int3(min(srcBase.x + 0u, maxX), min(srcBase.y + 1u, maxY), 0));
    const float d11 = SrcDepth.Load(int3(min(srcBase.x + 1u, maxX), min(srcBase.y + 1u, maxY), 0));

    const float nearestDepth = max(max(d00, d10), max(d01, d11));
    DstHiZMip[id.xy] = nearestDepth;
}

// -------------------------------------------------------------------------------------------------
// Atmosphere debug modes (deferred path with the Bruneton sky only). View.ShadingParameters.z stores the mode.
// A mode replaces the lighting output with the gray value s, and a reader decodes value = s * scale
// (value = (s - 0.5) * 2 * scale for the signed modes). Sky pixels are written magenta so that a reader can mask them.
//   1 heighterror    scale 1.0 m     |target height the runtime derives from the camera and the distance - target height from its position|
//   2 muerror        scale 0.0001    |mu_target the runtime derives - mu_target from the target position|
//   3 museam         signed, 0.05    mu_target - mu_horizon(h_target), runtime values
//   4 inscatter      scale 2.0       luminance of the aerial perspective in-scatter (AP1)
//   5 transmittance  scale 1.0       mean of the RGB segment transmittance
//   6 constant       s = 0.5         readback check
//   7 lutformula     scale 64 texels |u_mu by the OLD float formula - u_mu by the stable formula|, both on the old km chain
//                                    (shows the error that the fix removed; it does not run in the shading path)
//   8 luttotal       scale 64 texels |u_mu the runtime looks up - u_mu from the target position and the stable formula|
//   9 museamcam      signed, 0.05    mu_cam - mu_horizon(h_cam), runtime values
// -------------------------------------------------------------------------------------------------

internal static const uint ATMOS_DEBUG_OFF = 0u;
internal static const uint ATMOS_DEBUG_HEIGHT_ERROR = 1u;
internal static const uint ATMOS_DEBUG_MU_ERROR = 2u;
internal static const uint ATMOS_DEBUG_MU_SEAM = 3u;
internal static const uint ATMOS_DEBUG_INSCATTER = 4u;
internal static const uint ATMOS_DEBUG_TRANSMITTANCE = 5u;
internal static const uint ATMOS_DEBUG_CONSTANT = 6u;
internal static const uint ATMOS_DEBUG_LUT_FORMULA = 7u;
internal static const uint ATMOS_DEBUG_LUT_TOTAL = 8u;
internal static const uint ATMOS_DEBUG_MU_SEAM_CAMERA = 9u;

internal struct AtmosphereDebugValues
{
    float HeightErrorMeters;
    float MuError;
    float MuSeam;
    float MuSeamCamera;
    float InscatterLuminance;
    float TransmittanceMean;
    float LutFormulaTexels;
    float LutTotalTexels;
};

internal uint GetAtmosphereDebugMode()
{
    return uint(View.ShadingParameters.z + 0.5f);
}

// mode 3 and 9: mu of the horizon at a height above the ground, from the height directly: 1 - (R/r)^2 = h (2R + h) / r^2
internal float HorizonMu(float heightMeters, float groundMeters)
{
    const float height = max(heightMeters, 0.0f);
    return -sqrt(height * (2.0f * groundMeters + height)) / (groundMeters + height);
}

// The u_mu formula that GetInscatterUV had before the fix: float on r and mu, with r^2 - Rg^2 as a difference of two
// large squares. Kept here only so that the lutformula mode can show its error. The runtime does not call it.
internal float OldFloatInscatterMuCoordinate(float r, float mu)
{
    const float Rg = Atmosphere.BottomRadiusKm;
    const float Rt = Atmosphere.TopRadiusKm;
    const float H = sqrt(max(0.0f, Rt * Rt - Rg * Rg));
    const float rho = sqrt(max(0.0f, r * r - Rg * Rg));
    const float mu_horiz = (r > 0.0f) ? (-rho / r) : 0.0f;

    float u_mu;

    if (mu < mu_horiz)
    {
        const float delta_g = r * r * (mu * mu - 1.0f) + Rg * Rg;
        const float d = -r * mu - sqrt(max(0.0f, delta_g));
        const float d_min = r - Rg;
        const float d_max = rho;
        const float denom = max(1e-4f, d_max - d_min);
        u_mu = clamp(0.5f - 0.5f * ((d - d_min) / denom), 0.0f, 0.499999f);
    }
    else
    {
        const float delta_t = r * r * (mu * mu - 1.0f) + Rt * Rt;
        const float d = -r * mu + sqrt(max(0.0f, delta_t));
        const float d_min = Rt - r;
        const float d_max = rho + H;
        const float denom = max(1e-4f, d_max - d_min);
        u_mu = clamp(0.5f + 0.5f * ((d - d_min) / denom), 0.5f, 1.0f);
    }

    return u_mu;
}

// The runtime chain of EvaluateAtmosphereAerialPerspective (the height and r * mu of the target that it looks up),
// against the same values taken from the target position directly, and the old chain in earth-centred km.
internal AtmosphereDebugValues ComputeAtmosphereDebugValues(
    float3 cameraWorld,
    float3 targetWorld,
    float3 toSun,
    AerialPerspectiveResult ap)
{
    const float groundKm = Atmosphere.BottomRadiusKm;
    const float groundMeters = groundKm * 1000.0f;

    // what the runtime looks up
    const AerialPerspectiveGeometry geometry = ComputeAerialPerspectiveGeometry(Atmosphere, cameraWorld, targetWorld, toSun);
    const float runtimeTargetHeightMeters = geometry.TargetHeightKm * 1000.0f;
    const float runtimeTargetRadiusKm = groundKm + geometry.TargetHeightKm;
    const float runtimeTargetMu = geometry.TargetRadiusTimesMuKm / runtimeTargetRadiusKm;
    const float runtimeCameraRadiusKm = groundKm + geometry.CameraHeightKm;
    const float runtimeCameraMu = geometry.CameraRadiusTimesMuKm / runtimeCameraRadiusKm;
    const float runtimeCoordinate = StableInscatterMuCoordinate(
        Atmosphere, geometry.TargetHeightKm, geometry.TargetRadiusTimesMuKm, geometry.RayIntersectsGround);

    // the same values from the target position directly
    const float3 offsetMeters = targetWorld - cameraWorld;
    const float distanceMeters = max(length(offsetMeters), 1.0e-3f);
    const float3 exactDir = offsetMeters / distanceMeters;
    const float targetHeightMeters = ExactHeightMeters(targetWorld, groundMeters);
    const float targetRadiusTimesMuMeters = dot(targetWorld, exactDir) + groundMeters * exactDir.y;
    const float directTargetMu = targetRadiusTimesMuMeters / (groundMeters + targetHeightMeters);
    const float cameraHeightMeters = ExactHeightMeters(cameraWorld, groundMeters);
    const float exactCoordinate = StableInscatterMuCoordinate(
        Atmosphere, targetHeightMeters * 0.001f, targetRadiusTimesMuMeters * 0.001f, geometry.RayIntersectsGround);

    // the old chain: positions in earth-centred km, then the old float formula, then the stable formula on the same inputs
    const float3 cameraPosKm = cameraWorld * 0.001f + float3(0.0f, groundKm, 0.0f);
    const float3 targetPosKm = targetWorld * 0.001f + float3(0.0f, groundKm, 0.0f);
    const float3 segment = targetPosKm - cameraPosKm;
    const float dist = length(segment);
    const float3 vDir = (dist > 1e-4f) ? (segment / dist) : float3(0.0f, 1.0f, 0.0f);
    const float oldTargetRadius = length(targetPosKm);
    const float oldTargetMu = dot(targetPosKm, vDir) / max(1e-4f, oldTargetRadius);
    const float oldCoordinate = OldFloatInscatterMuCoordinate(oldTargetRadius, oldTargetMu);
    const float stableOnOldInputs = StableInscatterMuCoordinate(
        Atmosphere,
        oldTargetRadius - groundKm,
        oldTargetRadius * oldTargetMu,
        RayIntersectsGroundFromHeight(Atmosphere, oldTargetRadius - groundKm, oldTargetRadius * oldTargetMu));

    AtmosphereDebugValues values;
    values.HeightErrorMeters = abs(runtimeTargetHeightMeters - targetHeightMeters);
    values.MuError = abs(runtimeTargetMu - directTargetMu);
    values.MuSeam = runtimeTargetMu - HorizonMu(runtimeTargetHeightMeters, groundMeters);
    values.MuSeamCamera = runtimeCameraMu - HorizonMu(cameraHeightMeters, groundMeters);
    values.InscatterLuminance = dot(ap.InscatterAP1, float3(0.2722287f, 0.6740818f, 0.0536895f));
    values.TransmittanceMean = (ap.Transmittance.x + ap.Transmittance.y + ap.Transmittance.z) * (1.0f / 3.0f);
    values.LutFormulaTexels = abs(oldCoordinate - stableOnOldInputs) * 127.0f;
    values.LutTotalTexels = abs(runtimeCoordinate - exactCoordinate) * 127.0f;

    return values;
}

// returns the gray value s of the mode, in [0, 1]
internal float EncodeAtmosphereDebugScalar(uint mode, AtmosphereDebugValues values)
{
    float scalar = 0.0f;

    if (mode == ATMOS_DEBUG_HEIGHT_ERROR)
    {
        scalar = values.HeightErrorMeters / 1.0f;
    }
    else if (mode == ATMOS_DEBUG_MU_ERROR)
    {
        scalar = values.MuError / 0.0001f;
    }
    else if (mode == ATMOS_DEBUG_MU_SEAM)
    {
        scalar = 0.5f + values.MuSeam / (2.0f * 0.05f);
    }
    else if (mode == ATMOS_DEBUG_INSCATTER)
    {
        scalar = values.InscatterLuminance / 2.0f;
    }
    else if (mode == ATMOS_DEBUG_TRANSMITTANCE)
    {
        scalar = values.TransmittanceMean;
    }
    else if (mode == ATMOS_DEBUG_CONSTANT)
    {
        scalar = 0.5f;
    }
    else if (mode == ATMOS_DEBUG_LUT_FORMULA)
    {
        scalar = values.LutFormulaTexels / 64.0f;
    }
    else if (mode == ATMOS_DEBUG_LUT_TOTAL)
    {
        scalar = values.LutTotalTexels / 64.0f;
    }
    else if (mode == ATMOS_DEBUG_MU_SEAM_CAMERA)
    {
        scalar = 0.5f + values.MuSeamCamera / (2.0f * 0.05f);
    }

    return saturate(scalar);
}

// -------------------------------------------------------------------------------------------------
// SSR debug modes. View.ShadingParameters.w stores the mode. A mode replaces the lighting output with the gray value
// s, and a reader decodes value = s * scale. Sky pixels are written magenta so that a reader can mask them. A pixel
// that the march does not reach (SSR off, or a reflection ray that points high) reads 0 in every mode.
//   1 hit        scale 1     1 when the march hits, 0 when it misses
//   2 weight     scale 2     the final hitWeight, which SsrIntensity caps (the UI allows 2)
//   3 step       scale 12    hit step + 1 (the march has 12 steps), 0 for a miss
//   4 thickness  scale 10 m  view depth in metres by which the ray is behind the scene surface at the hit
//   5 distance   scale 25 m  ray distance from the shaded point at the hit
// -------------------------------------------------------------------------------------------------

internal static const int SSR_STEP_COUNT = 12;
internal static const float SSR_MIN_THICKNESS_METERS = 0.25f;
internal static const float SSR_DISTANCE_FADE_START_METERS = 12.0f;
internal static const float SSR_DISTANCE_FADE_END_METERS = 22.0f;
internal static const float SSR_PIXEL_GATE_START = 1.5f;
internal static const float SSR_PIXEL_GATE_END = 4.5f;

internal static const uint SSR_DEBUG_OFF = 0u;
internal static const uint SSR_DEBUG_HIT = 1u;
internal static const uint SSR_DEBUG_WEIGHT = 2u;
internal static const uint SSR_DEBUG_STEP = 3u;
internal static const uint SSR_DEBUG_THICKNESS = 4u;
internal static const uint SSR_DEBUG_DISTANCE = 5u;

internal uint GetSsrDebugMode()
{
    return uint(View.ShadingParameters.w + 0.5f);
}

internal static const uint SURF_DEBUG_OFF = 0u;
internal static const uint SURF_DEBUG_NORMAL = 1u;
internal static const uint SURF_DEBUG_SLOPE = 2u;
internal static const uint SURF_DEBUG_STRETCH = 3u;
internal static const uint SURF_DEBUG_JACOBIAN = 4u;
internal static const uint SURF_DEBUG_LEAN_SIGMA = 5u;
internal static const uint SURF_DEBUG_MIP_LEVEL = 6u;
internal static const uint SURF_DEBUG_CASCADE_WEIGHTS = 7u;
internal static const uint SURF_DEBUG_REFLECT_DIR = 8u;
internal static const uint SURF_DEBUG_FOAM_TERMS = 9u;
internal static const uint SURF_DEBUG_SPECULAR_SPLIT = 10u;

internal uint GetSurfaceDebugMode()
{
    return uint(View.LodParameters.w + 0.5f);
}

internal float ViewLengthScale(uint cascade)
{
    return View.LengthScales[cascade >> 2u][cascade & 3u];
}

internal float ViewCascadeWeight(uint cascade)
{
    return View.CascadeWeights[cascade >> 2u][cascade & 3u];
}

internal float CascadeDistanceWeight(uint cascade, float distance)
{
    const float fadeStart = View.LodParameters.x * ViewLengthScale(cascade);
    return ViewCascadeWeight(cascade) * (1.0f - smoothstep(fadeStart, 2.0f * fadeStart, distance));
}

internal float EncodeSsrDebugScalar(uint mode, float hit, float weight, int hitStep, float thicknessMeters, float distanceMeters)
{
    switch (mode)
    {
    case SSR_DEBUG_HIT: return hit;
    case SSR_DEBUG_WEIGHT: return weight * 0.5f;
    case SSR_DEBUG_STEP: return float(hitStep + 1) / 12.0f;
    case SSR_DEBUG_THICKNESS: return saturate(thicknessMeters / 10.0f);
    case SSR_DEBUG_DISTANCE: return saturate(distanceMeters / 25.0f);
    default: return 0.0f;
    }
}

// -------------------------------------------------------------------------------------------------
// 3. OceanDeferredLightingCS
// Deferred shading with dual-depth refraction, wave-crest SSR, EON foam, and aerial perspective.
// -------------------------------------------------------------------------------------------------

// dispatch ((Push.Dimensions.x + 7) / 8, (Push.Dimensions.y + 7) / 8, 1)
[shader("compute")]
[numthreads(8, 8, 1)]
void OceanDeferredLightingCS(
    uint3 id : SV_DispatchThreadID,
    uniform DeferredLightingPushConstants Push)
{
    if (id.x >= Push.Dimensions.x || id.y >= Push.Dimensions.y)
    {
        return;
    }

    const int2 coord = int2(id.xy);
    const float oceanDepth = OceanDepthTexture.Load(int3(coord, 0));
    const float4 seabedColorSample = SeabedColorTexture.Load(int3(coord, 0));

    if (oceanDepth <= 0.0f)
    {
        if (GetSurfaceDebugMode() != SURF_DEBUG_OFF)
        {
            HdrLightingOutput[coord] = float4(0.05f, 0.05f, 0.08f, 1.0f);
            return;
        }

        if (GetSsrDebugMode() != SSR_DEBUG_OFF)
        {
            HdrLightingOutput[coord] = float4(1.0f, 0.0f, 1.0f, 1.0f);
            return;
        }

        if (Frame.UseBrunetonAtmosphere != 0u)
        {
            if (GetAtmosphereDebugMode() != ATMOS_DEBUG_OFF)
            {
                HdrLightingOutput[coord] = float4(1.0f, 0.0f, 1.0f, 1.0f);
                return;
            }

            const float2 uv = (float2(coord) + 0.5f) * Push.InvDimensions;
            const float2 ndc = uv * 2.0f - 1.0f;
            const float4 clipFar = float4(ndc.x, ndc.y, 0.0f, 1.0f);
            const float3 worldFar = ClipToWorld(clipFar, Push.InvViewProjection);
            const float3 viewDir = normalize(worldFar - View.CameraPositionTime.xyz);
            const float3 toSun = View.SunDirectionSeabedDepth.xyz;
            const float3 skyRadiance = EvaluateAtmosphereSkyRadiance(
                InscatterLut,
                TransmittanceLut,
                AtmosphereLinearSampler,
                Atmosphere,
                View.CameraPositionTime.xyz,
                viewDir,
                toSun
            );
            HdrLightingOutput[coord] = float4(skyRadiance * Frame.SkyIntensityScale, 1.0f);
            return;
        }
        HdrLightingOutput[coord] = seabedColorSample;
        return;
    }

    const float2 uv = (float2(coord) + 0.5f) * Push.InvDimensions;
    const float2 ndc = uv * 2.0f - 1.0f;
    const float4 oceanClip = float4(ndc.x, ndc.y, oceanDepth, 1.0f);
    const float3 oceanWorldPos = ClipToWorld(oceanClip, Push.InvViewProjection);

    const float4 gNormalSigma = GBufferNormalSigmaFiltered.Load(int3(coord, 0));
    const float2 normalXz = gNormalSigma.xy;
    const float normalY = sqrt(max(0.0f, 1.0f - dot(normalXz, normalXz)));
    float3 normal = normalize(float3(normalXz.x, normalY, normalXz.y));
    const float2 sigmaSq = gNormalSigma.zw;

    const float4 gFoamSss = GBufferFoamSss.Load(int3(coord, 0));
    const float whitecaps = gFoamSss.x;
    const float underwaterFoam = gFoamSss.y;
    const float foamRoughness = gFoamSss.z;
    const float displacementY = oceanWorldPos.y;

    const float4 gMotionFoam = GBufferMotionFoam.Load(int3(coord, 0));
    const float2 foamNormalXy = gMotionFoam.zw;

    const float3 toCamera = normalize(View.CameraPositionTime.xyz - oceanWorldPos);
    normal = RegularizeGrazingNormal(toCamera, normal, sigmaSq);
    const float3 toSun = View.SunDirectionSeabedDepth.xyz;

    const float3 tangentX = normalize(abs(normal.y) < 0.999f
        ? cross(float3(0.0f, 1.0f, 0.0f), normal)
        : float3(1.0f, 0.0f, 0.0f));
    const float3 tangentY = cross(normal, tangentX);

    // Dual-depth seabed reconstruction and refracted sampling
    const float seabedDepth = SeabedDepthTexture.Load(int3(coord, 0));
    float3 seabedWorldPos;
    if (seabedDepth > 0.0f)
    {
        const float4 seabedClip = float4(ndc.x, ndc.y, seabedDepth, 1.0f);
        seabedWorldPos = ClipToWorld(seabedClip, Push.InvViewProjection);
    }
    else
    {
        seabedWorldPos = float3(oceanWorldPos.x, -View.SunDirectionSeabedDepth.w, oceanWorldPos.z);
    }

    const float waterPathLength = max(0.1f, length(oceanWorldPos - seabedWorldPos));
    const float2 refractOffset = normalXz * (0.02f * saturate(waterPathLength * 0.1f));
    const float2 refractedUv = clamp(uv + refractOffset, float2(0.001f), float2(0.999f));
    const float3 refractedSeabedColor = SeabedColorTexture.SampleLevel(LinearClampSampler, refractedUv, 0.0f).rgb;

    const float depth = max(0.1f, View.SunDirectionSeabedDepth.w);
    const float3 waterExtinction = float3(0.22f, 0.045f, 0.015f);
    const float viewPathLength = depth / max(0.01f, toCamera.y);
    const float3 viewAttenuation = exp(-waterExtinction * viewPathLength);
    const float viewTransmission = dot(viewAttenuation, float3(0.2126f, 0.7152f, 0.0722f));
    float waterOpacity = saturate(1.0f - viewTransmission);
    waterOpacity = saturate(lerp(waterOpacity, 1.0f, smoothstep(1500.0f, 3000.0f, length(oceanWorldPos.xz - View.CameraPositionTime.xz))));

    // Sky reflection evaluation
    float3 reflectDir = reflect(-toCamera, normal);
    reflectDir.y = max(0.01f, reflectDir.y);
    reflectDir = normalize(reflectDir);

    float3 skyRadiance = float3(0.0f);
    if (Frame.UseBrunetonAtmosphere != 0u)
    {
        skyRadiance = EvaluateAtmosphereSkyRadiance(
            InscatterLut,
            TransmittanceLut,
            AtmosphereLinearSampler,
            Atmosphere,
            View.CameraPositionTime.xyz,
            reflectDir,
            toSun
        ) * Frame.SkyIntensityScale;
    }
    else if (Frame.UseSkyTexture != 0u)
    {
        const float phi = atan2(reflectDir.z, reflectDir.x);
        const float theta = acos(clamp(reflectDir.y, -1.0f, 1.0f));
        const float2 skyUv = float2(phi * (0.5f / 3.14159265f) + 0.5f, theta * (1.0f / 3.14159265f));
        const float roughnessBias = 0.5f * log2(max(1.0f, (sigmaSq.x + sigmaSq.y) * 128.0f));
        skyRadiance = SkyDomeTexture.SampleLevel(SkyDomeSampler, skyUv, roughnessBias).rgb * Frame.SkyIntensityScale;
    }
    else
    {
        // irradiance / pi is the cosine-weighted mean radiance around the reflection direction
        skyRadiance = EvaluateShIrradiance(reflectDir, Frame.SkyShCoeffs) * (DEFERRED_INV_PI * Frame.SkyIntensityScale);
    }

    // Screen-space wave crest reflections (SSR). A fixed 12 step linear march of about 22 m along the reflection
    // ray. It reads the full resolution depth only and never touches the Hi-Z mips. A step hits when the ray is behind
    // the depth surface by less than a thickness in metres of view depth. The thickness limit is the step length,
    // with a floor, because a ray that crosses the surface between two steps can be that far behind it. The hit
    // weight fades by thickness, by ray distance, by screen distance from the shaded pixel and by screen edge, so no
    // binary boundary remains. SsrIntensity is the maximum weight.
    float3 reflectionColor = skyRadiance;
    float ssrDebugHit = 0.0f;
    float ssrDebugWeight = 0.0f;
    int ssrDebugStep = -1;
    float ssrDebugThickness = 0.0f;
    float ssrDebugDistance = 0.0f;
    if (Frame.SsrIntensity > 0.0f && reflectDir.y < 0.25f)
    {
        float hitWeight = 0.0f;
        float3 hitColor = float3(0.0f);
        float rayDist = 0.2f;
        int hitStep = -1;
        float hitThicknessMeters = 0.0f;

        [unroll]
        for (int step = 0; step < SSR_STEP_COUNT; ++step)
        {
            const float stepLength = 0.3f * pow(1.3f, float(step));
            rayDist += stepLength;
            const float3 marchPos = oceanWorldPos + reflectDir * rayDist;
            const float4 marchClip = float4(
                dot(View.ViewProjectionRows[0], float4(marchPos, 1.0f)),
                dot(View.ViewProjectionRows[1], float4(marchPos, 1.0f)),
                dot(View.ViewProjectionRows[2], float4(marchPos, 1.0f)),
                dot(View.ViewProjectionRows[3], float4(marchPos, 1.0f))
            );

            if (marchClip.w <= 0.01f)
            {
                continue;
            }

            const float2 marchNdc = marchClip.xy / marchClip.w;
            const float2 marchUv = marchNdc * 0.5f + 0.5f;
            const float marchDepth = marchClip.z / marchClip.w;

            if (!all(marchUv >= 0.0f) || !all(marchUv <= 1.0f))
            {
                continue;
            }

            // a ray that stays inside the shaded pixel reads the pixel's own depth, which is a false hit
            const float pixelDistance = length((marchUv - uv) * float2(Push.Dimensions));
            const float pixelGate = smoothstep(SSR_PIXEL_GATE_START, SSR_PIXEL_GATE_END, pixelDistance);

            if (pixelGate <= 0.0f)
            {
                continue;
            }

            const float sceneDepth = OceanDepthTexture.SampleLevel(PointClampSampler, marchUv, 0.0f);

            if (sceneDepth <= marchDepth)
            {
                continue;
            }

            const float3 sceneWorldPos = ClipToWorld(float4(marchNdc, sceneDepth, 1.0f), Push.InvViewProjection);
            const float sceneViewDepth = dot(View.ViewProjectionRows[3], float4(sceneWorldPos, 1.0f));
            const float thicknessMeters = marchClip.w - sceneViewDepth;
            const float thicknessLimit = max(SSR_MIN_THICKNESS_METERS, stepLength);

            if (thicknessMeters <= 0.0f || thicknessMeters >= thicknessLimit)
            {
                continue;
            }

            const float thicknessFade = 1.0f - smoothstep(0.5f * thicknessLimit, thicknessLimit, thicknessMeters);
            const float distanceFade = 1.0f - smoothstep(SSR_DISTANCE_FADE_START_METERS, SSR_DISTANCE_FADE_END_METERS, rayDist);
            const float edgeFade = saturate(1.0f - max(abs(marchUv.x * 2.0f - 1.0f), abs(marchUv.y * 2.0f - 1.0f)));
            const float3 sampledColor = HistoryColorTexture.SampleLevel(LinearClampSampler, marchUv, 0.0f).rgb;

            hitStep = step;
            hitThicknessMeters = thicknessMeters;

            if (!any(isnan(sampledColor)) && !any(isinf(sampledColor)))
            {
                hitColor = max(float3(0.0f), sampledColor);
                hitWeight = edgeFade * thicknessFade * distanceFade * pixelGate * Frame.SsrIntensity;
            }

            break;
        }

        reflectionColor = lerp(skyRadiance, hitColor, hitWeight);

        ssrDebugHit = hitStep >= 0 ? 1.0f : 0.0f;
        ssrDebugWeight = hitWeight;
        ssrDebugStep = hitStep;
        ssrDebugThickness = hitThicknessMeters;
        ssrDebugDistance = hitStep >= 0 ? rayDist : 0.0f;
    }

    const uint ssrDebugMode = GetSsrDebugMode();
    if (ssrDebugMode != SSR_DEBUG_OFF)
    {
        const float ssrScalar = EncodeSsrDebugScalar(ssrDebugMode, ssrDebugHit, ssrDebugWeight, ssrDebugStep, ssrDebugThickness, ssrDebugDistance);
        HdrLightingOutput[coord] = float4(ssrScalar, ssrScalar, ssrScalar, 1.0f);
        return;
    }

    // Ross-Bruneton-LEADR specular BRDF and Bruneton Fresnel
    const float effectiveFresnel = EvaluateBrunetonFresnel(toCamera, normal, sigmaSq);
    const float specularGain = max(0.0f, View.ShadingParameters.y);
    const float specularSun = EvaluateOceanSpecular(
        toSun,
        toCamera,
        normal,
        tangentX,
        tangentY,
        sigmaSq
    ) * specularGain;
    const float3 sunSpecularRadiance = Frame.SunRadianceAp1 * specularSun;

    const float tUf = 1.0f - underwaterFoam;
    const float tUf2 = tUf * tUf;
    const float tUf5 = tUf2 * tUf2 * tUf;
    const float specularMask = tUf5 * (1.0f - whitecaps);

    // Directional crest SSS and water body scattering
    const float dSunCam = saturate(dot(-toSun, toCamera));
    const float dSunCam2 = dSunCam * dSunCam;
    const float forwardScatter = dSunCam2 * dSunCam2;
    const float crestFactor = saturate(max(0.0f, displacementY) * 0.8f + 0.1f);
    // the author's scatter terms from before the deferred pass: the body scatter follows the reflected sky, so the
    // swells pick up the sky's variation
    const float3 crestSss = Frame.SunRadianceAp1 * Frame.WaterScatterColorAp1 * (forwardScatter * crestFactor * Frame.SssStrength);
    // body radiance = water-leaving reflectance (sr^-1) x downwelling irradiance: the sun on a horizontal surface plus
    // the sky. The reflectance is that of clear open ocean
    const float3 skyDownwelling = EvaluateShIrradiance(float3(0.0f, 1.0f, 0.0f), Frame.SkyShCoeffs) * Frame.SkyIntensityScale;
    const float3 downwellingIrradiance = saturate(toSun.y) * Frame.SunRadianceAp1 + skyDownwelling;
    const float3 bodyScatter = Frame.WaterLeavingReflectanceAp1 * (Frame.BodyReflectanceScale * downwellingIrradiance);

    const float3 underwaterColor = float3(0.05f, 0.14f, 0.16f);
    const float3 waterSubsurface = lerp(bodyScatter, underwaterColor * Frame.WaterScatterColorAp1, underwaterFoam) + crestSss;

    const float3 underwaterRadiance = (seabedDepth > 0.0f)
        ? lerp(refractedSeabedColor, waterSubsurface, waterOpacity)
        : (waterSubsurface * waterOpacity);

    const float3 waterRadiance = lerp(underwaterRadiance, reflectionColor, effectiveFresnel)
        + sunSpecularRadiance * specularMask;

    // Portsmouth et al. (2024) EON rough diffuse foam
    float3 foamRadiance = float3(0.0f);
    if (whitecaps > 1.0e-4f)
    {
        const float3 foamAlbedo = gFoamSss.w * View.FoamTextureTint.xyz;
        const float3 foamNormal = normalize(normal + View.FoamTextureParameters.y * float3(foamNormalXy.x, 0.0f, foamNormalXy.y));
        const float3 foamDirect = EvaluateEonDirect(foamAlbedo, foamRoughness, foamNormal, toCamera, toSun) * Frame.SunRadianceAp1;
        const float3 skyIrradiance = EvaluateShIrradiance(foamNormal, Frame.SkyShCoeffs) * Frame.SkyIntensityScale;
        const float3 foamIbl = EvaluateEonIbl(foamAlbedo, foamRoughness, foamNormal, toCamera, skyIrradiance);
        foamRadiance = foamDirect + foamIbl;
    }

    float3 finalColor = lerp(waterRadiance, foamRadiance, whitecaps);

    const uint surfDebugMode = GetSurfaceDebugMode();
    if (surfDebugMode != SURF_DEBUG_OFF)
    {
        float3 debugColor = float3(0.0f);
        if (surfDebugMode == SURF_DEBUG_NORMAL)
        {
            debugColor = float3(normal.x * 0.5f + 0.5f, saturate(normal.y), normal.z * 0.5f + 0.5f);
        }
        else if (surfDebugMode == SURF_DEBUG_SLOPE
              || surfDebugMode == SURF_DEBUG_STRETCH
              || surfDebugMode == SURF_DEBUG_JACOBIAN
              || surfDebugMode == SURF_DEBUG_MIP_LEVEL
              || surfDebugMode == SURF_DEBUG_CASCADE_WEIGHTS)
        {
            debugColor = gFoamSss.xyz;
        }
        else if (surfDebugMode == SURF_DEBUG_LEAN_SIGMA)
        {
            debugColor = float3(saturate(sigmaSq.x * 10.0f), saturate(sigmaSq.y * 10.0f), 0.0f);
        }
        else if (surfDebugMode == SURF_DEBUG_REFLECT_DIR)
        {
            const float3 rawReflect = reflect(-toCamera, normal);
            if (rawReflect.y <= 0.0f)
            {
                debugColor = float3(1.0f, 0.0f, 0.0f);
            }
            else
            {
                const float elev = saturate(sqrt(max(0.0f, rawReflect.y)));
                debugColor = float3(
                    (rawReflect.x * 0.5f + 0.5f) * 0.40f,
                    0.25f + 0.75f * elev,
                    (rawReflect.z * 0.5f + 0.5f) * 0.80f);
            }
        }
        else if (surfDebugMode == SURF_DEBUG_FOAM_TERMS)
        {
            debugColor = float3(saturate(whitecaps), saturate(1.0f - specularMask), 0.0f);
        }
        else if (surfDebugMode == SURF_DEBUG_SPECULAR_SPLIT)
        {
            const float3 ap1Lum = float3(0.2722287f, 0.6740818f, 0.0536895f);
            const float reflLum = dot(reflectionColor * effectiveFresnel, ap1Lum);
            const float specLum = dot(sunSpecularRadiance * specularMask, ap1Lum);
            const float sssLum = dot(waterSubsurface * waterOpacity * (1.0f - effectiveFresnel), ap1Lum);
            debugColor = float3(saturate(reflLum * 0.5f), saturate(specLum * 0.5f), saturate(sssLum * 4.0f));
        }

        HdrLightingOutput[coord] = float4(debugColor, 1.0f);
        return;
    }

    // Bruneton (2008) Aerial Perspective
    if (Frame.UseBrunetonAtmosphere != 0u)
    {
        const AerialPerspectiveResult ap = EvaluateAtmosphereAerialPerspective(
            InscatterLut,
            TransmittanceLut,
            AtmosphereLinearSampler,
            Atmosphere,
            View.CameraPositionTime.xyz,
            oceanWorldPos,
            toSun
        );
        finalColor = finalColor * ap.Transmittance + ap.InscatterAP1;

        const uint debugMode = GetAtmosphereDebugMode();
        if (debugMode != ATMOS_DEBUG_OFF)
        {
            const AtmosphereDebugValues debugValues = ComputeAtmosphereDebugValues(View.CameraPositionTime.xyz, oceanWorldPos, toSun, ap);
            const float debugScalar = EncodeAtmosphereDebugScalar(debugMode, debugValues);
            HdrLightingOutput[coord] = float4(debugScalar, debugScalar, debugScalar, 1.0f);
            return;
        }
    }
    else
    {
        const float horizDist = length(oceanWorldPos.xz - View.CameraPositionTime.xz);
        const float haze = smoothstep(10000.0f, 60000.0f, horizDist);
        finalColor = lerp(finalColor, skyRadiance, haze * 0.8f);
    }

    HdrLightingOutput[coord] = float4(finalColor, 1.0f);
}

// -------------------------------------------------------------------------------------------------
// 4. OceanTaaResolveCS
// Temporal Antialiasing with Karis weighting and YCoCg variance clipping.
// -------------------------------------------------------------------------------------------------

// dispatch ((Push.Dimensions.x + 7) / 8, (Push.Dimensions.y + 7) / 8, 1)
[shader("compute")]
[numthreads(8, 8, 1)]
void OceanTaaResolveCS(
    uint3 id : SV_DispatchThreadID,
    uniform TaaPushConstants Push)
{
    if (id.x >= Push.Dimensions.x || id.y >= Push.Dimensions.y)
    {
        return;
    }

    const int2 coord = int2(id.xy);
    const float4 currentColor = HdrLightingInput.Load(int3(coord, 0));

    if (Frame.UseTaa == 0u)
    {
        HdrResolvedOutput[coord] = currentColor;
        NextHistoryOutput[coord] = currentColor;
        return;
    }

    const float2 uv = (float2(coord) + 0.5f) * Push.InvDimensions;
    const float4 motionFoam = GBufferMotionFoam.Load(int3(coord, 0));
    const float2 motionUv = motionFoam.xy;
    const float2 historyUv = uv - motionUv;
    const bool offscreen = any(historyUv < float2(0.0f)) || any(historyUv > float2(1.0f));

    // Gather 3x3 neighborhood moments in YCoCg space
    float3 m1 = float3(0.0f);
    float3 m2 = float3(0.0f);

    [unroll]
    for (int dy = -1; dy <= 1; ++dy)
    {
        [unroll]
        for (int dx = -1; dx <= 1; ++dx)
        {
            const int2 neighborCoord = clamp(coord + int2(dx, dy), int2(0), int2(Push.Dimensions - 1u));
            const float3 cRgb = HdrLightingInput.Load(int3(neighborCoord, 0)).rgb;
            const float3 cYCoCg = RgbToYCoCg(cRgb);
            m1 += cYCoCg;
            m2 += cYCoCg * cYCoCg;
        }
    }

    const float3 mu = m1 * (1.0f / 9.0f);
    const float3 sigma = sqrt(max(float3(0.0f), m2 * (1.0f / 9.0f) - mu * mu));
    const float gamma = 1.25f;
    const float3 boxMin = mu - gamma * sigma;
    const float3 boxMax = mu + gamma * sigma;

    float3 historyRgb = HistoryColorTexture.SampleLevel(LinearClampSampler, historyUv, 0.0f).rgb;
    float3 historyYCoCg = RgbToYCoCg(historyRgb);
    historyYCoCg = clamp(historyYCoCg, boxMin, boxMax);
    historyRgb = YCoCgToRgb(historyYCoCg);

    // Karis high-contrast luminance weighting
    const float lumWeights = dot(currentColor.rgb, float3(0.2722287f, 0.6740818f, 0.0536895f));
    const float histLumWeights = dot(historyRgb, float3(0.2722287f, 0.6740818f, 0.0536895f));
    const float wCurrent = 1.0f / (1.0f + max(0.0f, lumWeights));
    const float wHistory = 1.0f / (1.0f + max(0.0f, histLumWeights));

    const float alpha = (offscreen || Frame.UseTaa == 0u) ? 1.0f : clamp(Frame.TaaBlendAlpha, 0.01f, 0.5f);
    const float3 resolvedRgb = (currentColor.rgb * wCurrent * alpha + historyRgb * wHistory * (1.0f - alpha))
        / max(1e-5f, wCurrent * alpha + wHistory * (1.0f - alpha));

    HdrResolvedOutput[coord] = float4(resolvedRgb, 1.0f);
    NextHistoryOutput[coord] = float4(resolvedRgb, 1.0f);
}

// -------------------------------------------------------------------------------------------------
// 5. OceanAutoExposureCS
// Log2 luminance reduction and exponential eye adaptation.
// -------------------------------------------------------------------------------------------------

// dispatch (1, 1, 1)
[shader("compute")]
[numthreads(16, 16, 1)]
void OceanAutoExposureCS(
    uint3 groupThreadId : SV_GroupThreadID,
    uniform AutoExposurePushConstants Push)
{
    const uint threadIdx = groupThreadId.y * 16u + groupThreadId.x;
    float threadSum = 0.0f;
    uint sampleCount = 0u;

    for (uint y = groupThreadId.y; y < Push.Dimensions.y; y += 16u)
    {
        for (uint x = groupThreadId.x; x < Push.Dimensions.x; x += 16u)
        {
            const float3 rgb = HdrResolvedInput.Load(int3(x, y, 0)).rgb;
            const float lum = dot(rgb, float3(0.2722287f, 0.6740818f, 0.0536895f));
            const float logLum = clamp(log2(max(1e-4f, lum)), Push.MinLogLum, Push.MaxLogLum);
            threadSum += logLum;
            sampleCount += 1u;
        }
    }

    s_SharedLogLum[threadIdx] = threadSum;
    GroupMemoryBarrierWithGroupSync();

    [unroll]
    for (uint stride = 128u; stride > 0u; stride >>= 1u)
    {
        if (threadIdx < stride)
        {
            s_SharedLogLum[threadIdx] += s_SharedLogLum[threadIdx + stride];
        }
        GroupMemoryBarrierWithGroupSync();
    }

    if (threadIdx == 0u)
    {
        const float totalPixels = float(Push.Dimensions.x * Push.Dimensions.y);
        const float avgLogLum = s_SharedLogLum[0] / max(1.0f, totalPixels);
        const float targetLum = exp2(avgLogLum);

        const float4 prevState = ExposureStateBuffer[0];
        const float prevAdaptedLum = (prevState.x > 1e-4f) ? prevState.x : targetLum;

        const float speed = max(0.01f, Push.AdaptationSpeed);
        const float adaptedLum = prevAdaptedLum + (targetLum - prevAdaptedLum) * (1.0f - exp(-Push.DeltaTime * speed));

        const float targetEv = log2(max(1e-4f, targetLum * 8.0f));
        const float adaptedEv = log2(max(1e-4f, adaptedLum * 8.0f));

        ExposureStateBuffer[0] = float4(adaptedLum, targetEv, adaptedEv, avgLogLum);
    }
}
