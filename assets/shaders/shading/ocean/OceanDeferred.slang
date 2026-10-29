module OceanDeferred;

import LodestoneAttributes;
import LodestoneShims;
import OceanAtmosphere;
import ddColor;

// OceanDeferred.slang
// Deferred ocean rendering post-passes:
// 1. OceanSpecularAaCS: Toksvig / Kaplanyan (2016) Screen-Space Specular Normal Variance AA.
// 2. OceanHiZDownsampleCS: Reverse-Z hierarchical depth downsampling.
// 3. OceanDeferredLightingCS: Deferred shading, dual-depth refraction, SSR, and aerial perspective.
// 4. OceanTaaResolveCS: Variance-clipped Karis-weighted Temporal Antialiasing.
// 5. OceanAutoExposureCS: Single-pass shared-memory log2 luminance reduction and eye adaptation.

internal static const float DEFERRED_PI = 3.14159265358979323846f;
internal static const float DEFERRED_INV_PI = 0.31830988618379067154f;
internal static const float DEFERRED_CONST1_FON = 0.287813735748f;
internal static const float DEFERRED_CONST2_FON = 0.072237894236f;

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

[ForceInline]
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

[ForceInline]
internal float Erfc(float x)
{
    return 2.0f * exp(-x * x) / (2.319f * x + sqrt(4.0f + 1.52f * x * x));
}

[ForceInline]
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

[ForceInline]
internal float EvaluateBrunetonMeanFresnel(float cosThetaV, float sigmaV)
{
    const float exponent = 5.0f * exp(-2.69f * sigmaV);
    const float denom = 1.0f + 22.7f * pow(max(0.0f, sigmaV), 1.5f);
    return pow(max(0.0f, 1.0f - cosThetaV), exponent) / max(1e-5f, denom);
}

[ForceInline]
internal float EvaluateBrunetonFresnel(float3 V, float3 N, float2 sigmaSq)
{
    const float cosThetaV = saturate(dot(V, N));
    const float sigmaV = sqrt(0.5f * (sigmaSq.x + sigmaSq.y));
    return 0.02f + 0.98f * EvaluateBrunetonMeanFresnel(cosThetaV, sigmaV);
}

[ForceInline]
internal float DeferredFastAcosPositive(float x)
{
    const float cx = saturate(x);
    return sqrt(1.0f - cx) * (1.5707963f + cx * (-0.2145988f + cx * (0.0889789f - 0.0501743f * cx)));
}

internal float EvaluateFonSingleDirectionalAlbedo(float mu, float r, float Af, float Bf)
{
    const float muClamped = saturate(mu);
    const float s = sqrt(max(0.0f, 1.0f - muClamped * muClamped));
    const float safeMu = max(1e-5f, muClamped);
    const float acosMu = DeferredFastAcosPositive(muClamped);
    const float g = s * (acosMu - s * muClamped) + (2.0f / 3.0f) * ((s / safeMu) * (1.0f - s * s * s) - s);
    return Af + (Bf * DEFERRED_INV_PI) * g;
}

internal float3 EvaluateEonDirectionalAlbedo(float3 rho, float r, float mu)
{
    const float roughness = clamp(r, 0.0f, 1.0f);
    if (roughness <= 1e-4f)
    {
        return saturate(rho);
    }

    const float af = 1.0f / (1.0f + DEFERRED_CONST1_FON * roughness);
    const float bf = roughness * af;
    const float avgEf = af * (1.0f + DEFERRED_CONST2_FON * roughness);

    const float ef = EvaluateFonSingleDirectionalAlbedo(mu, roughness, af, bf);
    const float3 clampedRho = saturate(rho);
    const float3 denom = max(float3(1e-6f), float3(1.0f) - clampedRho * (1.0f - avgEf));
    const float3 rhoMs = (clampedRho * clampedRho) * avgEf / denom;

    return clampedRho * ef + rhoMs * max(0.0f, 1.0f - ef);
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
        return clampedRho * (DEFERRED_INV_PI * nDotL);
    }

    const float af = 1.0f / (1.0f + DEFERRED_CONST1_FON * roughness);
    const float bf = roughness * af;
    const float avgEf = af * (1.0f + DEFERRED_CONST2_FON * roughness);

    const float s = dot(V, L) - nDotL * nDotV;
    const float sOverT = s > 0.0f ? s / max(nDotL, nDotV) : s;
    const float3 fSs = (clampedRho * DEFERRED_INV_PI) * (af * (1.0f + roughness * sOverT));

    const float efL = EvaluateFonSingleDirectionalAlbedo(nDotL, roughness, af, bf);
    const float efV = EvaluateFonSingleDirectionalAlbedo(nDotV, roughness, af, bf);

    const float3 denomMs = max(float3(1e-6f), float3(1.0f) - clampedRho * (1.0f - avgEf));
    const float3 rhoMs = (clampedRho * clampedRho) * avgEf / denomMs;

    const float compL = max(0.0f, 1.0f - efL);
    const float compV = max(0.0f, 1.0f - efV);
    const float denomAvg = max(1e-6f, 1.0f - avgEf);
    const float3 fMs = (rhoMs * DEFERRED_INV_PI) * ((compL * compV) / denomAvg);

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
internal float3 RgbToYCoCg(float3 c)
{
    return float3(
        0.25f * c.r + 0.5f * c.g + 0.25f * c.b,
        0.5f * c.r - 0.5f * c.b,
       -0.25f * c.r + 0.5f * c.g - 0.25f * c.b
    );
}

[ForceInline]
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
        if (Frame.UseBrunetonAtmosphere != 0u)
        {
            const float2 uv = (float2(coord) + 0.5f) * Push.InvDimensions;
            const float2 ndc = uv * 2.0f - 1.0f;
            const float4 clipFar = float4(ndc.x, ndc.y, 0.0f, 1.0f);
            const float3 worldFar = ClipToWorld(clipFar, Push.InvViewProjection);
            const float3 viewDir = normalize(worldFar - View.CameraPositionTime.xyz);
            const float3 toSun = View.SunDirectionSeabedDepth.xyz;
            const float3 cameraPosKm = View.CameraPositionTime.xyz * 0.001f + float3(0.0f, Atmosphere.BottomRadiusKm, 0.0f);
            const float3 skyRadiance = EvaluateAtmosphereSkyRadiance(
                InscatterLut,
                TransmittanceLut,
                AtmosphereLinearSampler,
                Atmosphere,
                cameraPosKm,
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
    const float3 normal = normalize(float3(normalXz.x, normalY, normalXz.y));
    const float2 sigmaSq = gNormalSigma.zw;

    const float4 gFoamSss = GBufferFoamSss.Load(int3(coord, 0));
    const float whitecaps = gFoamSss.x;
    const float underwaterFoam = gFoamSss.y;
    const float foamRoughness = gFoamSss.z;
    const float displacementY = oceanWorldPos.y;

    const float4 gMotionFoam = GBufferMotionFoam.Load(int3(coord, 0));
    const float2 foamNormalXy = gMotionFoam.zw;

    const float3 toCamera = normalize(View.CameraPositionTime.xyz - oceanWorldPos);
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
        const float3 cameraPosKm = View.CameraPositionTime.xyz * 0.001f + float3(0.0f, Atmosphere.BottomRadiusKm, 0.0f);
        skyRadiance = EvaluateAtmosphereSkyRadiance(
            InscatterLut,
            TransmittanceLut,
            AtmosphereLinearSampler,
            Atmosphere,
            cameraPosKm,
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
        skyRadiance = EvaluateSkyShIrradiance(reflectDir, Frame.SkyShCoeffs) * Frame.SkyIntensityScale;
    }

    // Hi-Z / Screen-Space wave crest reflections (SSR)
    float3 reflectionColor = skyRadiance;
    if (Frame.SsrIntensity > 0.0f && reflectDir.y < 0.25f)
    {
        float hitWeight = 0.0f;
        float3 hitColor = float3(0.0f);
        float rayDist = 0.2f;

        [unroll]
        for (int step = 0; step < 12; ++step)
        {
            rayDist += 0.3f * pow(1.3f, float(step));
            const float3 marchPos = oceanWorldPos + reflectDir * rayDist;
            const float4 marchClip = float4(
                dot(View.ViewProjectionRows[0], float4(marchPos, 1.0f)),
                dot(View.ViewProjectionRows[1], float4(marchPos, 1.0f)),
                dot(View.ViewProjectionRows[2], float4(marchPos, 1.0f)),
                dot(View.ViewProjectionRows[3], float4(marchPos, 1.0f))
            );

            if (marchClip.w > 0.01f)
            {
                const float2 marchUv = (marchClip.xy / marchClip.w) * 0.5f + 0.5f;
                const float marchDepth = marchClip.z / marchClip.w;

                if (all(marchUv >= 0.0f) && all(marchUv <= 1.0f))
                {
                    const float sceneDepth = OceanDepthTexture.SampleLevel(PointClampSampler, marchUv, 0.0f);
                    const float thickness = (sceneDepth - marchDepth) / max(sceneDepth, 1e-4f);
                    if (sceneDepth > marchDepth && thickness > 0.0f && thickness < 0.05f)
                    {
                        const float edgeFade = saturate(1.0f - max(abs(marchUv.x * 2.0f - 1.0f), abs(marchUv.y * 2.0f - 1.0f)));
                        const float3 sampledColor = HistoryColorTexture.SampleLevel(LinearClampSampler, marchUv, 0.0f).rgb;
                        if (!any(isnan(sampledColor)) && !any(isinf(sampledColor)))
                        {
                            hitColor = max(float3(0.0f), sampledColor);
                            hitWeight = edgeFade * Frame.SsrIntensity;
                        }
                        break;
                    }
                }
            }
        }
        reflectionColor = lerp(skyRadiance, hitColor, hitWeight);
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
    const float3 clampedSunRadiance = min(Frame.SunRadianceAp1, float3(12.0f));
    const float3 crestSss = clampedSunRadiance * Frame.WaterScatterColorAp1 * (forwardScatter * crestFactor * Frame.SssStrength);
    const float3 skyZenithIrradiance = EvaluateSkyShIrradiance(float3(0.0f, 1.0f, 0.0f), Frame.SkyShCoeffs) * Frame.SkyIntensityScale;
    const float3 bodyScatter = Frame.WaterScatterColorAp1 * (saturate(toSun.y) * Frame.SunRadianceAp1 * 0.04f + skyZenithIrradiance * 0.08f);

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
        const float3 skyIrradiance = EvaluateSkyShIrradiance(foamNormal, Frame.SkyShCoeffs) * Frame.SkyIntensityScale;
        const float3 foamIbl = EvaluateEonIbl(foamAlbedo, foamRoughness, foamNormal, toCamera, skyIrradiance);
        foamRadiance = foamDirect + foamIbl;
    }

    float3 finalColor = lerp(waterRadiance, foamRadiance, whitecaps);

    // Bruneton (2008) Aerial Perspective
    if (Frame.UseBrunetonAtmosphere != 0u)
    {
        const float3 cameraPosKm = View.CameraPositionTime.xyz * 0.001f + float3(0.0f, Atmosphere.BottomRadiusKm, 0.0f);
        const float3 targetPosKm = oceanWorldPos * 0.001f + float3(0.0f, Atmosphere.BottomRadiusKm, 0.0f);
        const AerialPerspectiveResult ap = EvaluateAtmosphereAerialPerspective(
            InscatterLut,
            TransmittanceLut,
            AtmosphereLinearSampler,
            Atmosphere,
            cameraPosKm,
            targetPosKm,
            toSun
        );
        finalColor = finalColor * ap.Transmittance + ap.InscatterAP1;
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
