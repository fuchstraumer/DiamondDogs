implementing OceanRender;

// Ocean surface shading module.
// Implements the Ross-Bruneton-LEADR anisotropic specular BRDF,
// Bruneton effective Fresnel reflection, LEADR-biased sky reflections,
// AP1 forward crest subsurface scattering, and Portsmouth et al. (2024) EON foam shading.

internal struct SurfaceSample
{
    float3 Normal;
    float2 SigmaSq;
    float DisplacementY;
};

internal void SampleSurfaceAndWhitecaps(
    float2 surface_xz,
    float2 ddxXz,
    float2 ddyXz,
    float wave_height,
    float threshold_bias,
    out SurfaceSample surface,
    out float whitecaps)
{
    float2 totalSlope = float2(0.0f);
    float2 totalStretch = float2(0.0f);
    float2 unresolvedVariance = float2(0.0f);
    const float distance = length(surface_xz - View.CameraPositionTime.xz);
    const float footprint = max(length(ddxXz), length(ddyXz));

    float muA = 0.0f;
    float varA = 0.0f;
    float3 tensor = float3(0.0f);
    const float rest = 1.0f / float(OCEAN_CASCADE_COUNT);

    [unroll]
    for (uint cascade = 0; cascade < OCEAN_CASCADE_COUNT; ++cascade)
    {
        const float weight = CascadeDistanceWeight(cascade, distance);
        const float foamWeight = ViewFoamWeight(cascade);

        if (weight > 0.0f || foamWeight > 0.0f)
        {
            const float2 ddxUv = WorldToCascade(ddxXz, cascade) / ViewLengthScale(cascade);
            const float2 ddyUv = WorldToCascade(ddyXz, cascade) / ViewLengthScale(cascade);
            const float3 uvw = float3(CascadeUv(surface_xz, cascade), float(cascade));
            const float4 derivatives = Derivatives.SampleGrad(OceanSampler, uvw, ddxUv, ddyUv);

            if (weight > 0.0f)
            {
                const float texelSize = ViewLengthScale(cascade) / View.LodParameters.y;
                const float lodFactor = saturate(log2(max(1.0f, footprint / texelSize)) * 0.25f);
                const float filterWeight = weight * (1.0f - lodFactor);

                const float4 scaledDeriv = derivatives * filterWeight;
                const float2 worldSlope = CascadeToWorld(scaledDeriv.xy, cascade);
                totalSlope += worldSlope;
                totalStretch += CascadeStretchToWorld(scaledDeriv.zw, cascade);

                const float2 rawWorldSlope = CascadeToWorld(derivatives.xy, cascade);
                unresolvedVariance += (1.0f - filterWeight) * (rawWorldSlope * rawWorldSlope);
            }

            if (foamWeight > 0.0f)
            {
                const float jacobianDeviation = Displacement.Sample(OceanSampler, uvw).w - 1.0f;
                const float kMean = jacobianDeviation + rest;
                const float2 moments = Whitecaps.Sample(OceanSampler, uvw);
                const float2 stretch = derivatives.zw;

                muA += rest + foamWeight * jacobianDeviation;
                varA += foamWeight * foamWeight * max(0.0f, moments.x - kMean * kMean);
                tensor += foamWeight * CascadeTensorToWorld(float3(stretch, moments.y), cascade);
            }
            else
            {
                muA += rest;
            }
        }
        else
        {
            muA += rest;
        }
    }

    const float2 slopeFwidth = fwidth(totalSlope);
    unresolvedVariance += 0.5f * (slopeFwidth * slopeFwidth);

    const float2 slope = totalSlope / max(float2(0.1f), 1.0f + totalStretch);
    surface.Normal = normalize(float3(-slope.x, 1.0f, -slope.y));

    const float2 baseVariance = float2(0.005f, 0.005f);
    const float2 leadrVariance = unresolvedVariance * Frame.LeadrSlopeVarianceScale;
    surface.SigmaSq = max(float2(1e-4f), baseVariance + leadrVariance);
    surface.DisplacementY = wave_height;

    // Crest sharpness
    const float jxx = 1.0f + tensor.x;
    const float jzz = 1.0f + tensor.y;
    const float halfDifference = 0.5f * (jxx - jzz);
    const float minorEigenvalue = 0.5f * (jxx + jzz) - sqrt(halfDifference * halfDifference + tensor.z * tensor.z);
    muA = lerp(muA, minorEigenvalue, saturate(View.WhitecapParameters.z));

    const float sigmaSq = max(1e-4f, varA);
    const float sigmaA = sqrt(sigmaSq);

    const float threshold = clamp(View.WhitecapParameters.x, -0.5f, 1.5f);
    const float epsilon = threshold * 0.4f - 0.2f + threshold_bias;
    const float errfIn = 0.70710678f * (epsilon - muA) / sigmaA;
    const float rawCoverage = saturate(0.5f + (0.5f * FastErf(errfIn)));
    const float foamAmount = max(0.0f, View.WhitecapParameters.y);
    whitecaps = saturate(rawCoverage * foamAmount);
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
    const float cosThetaV = saturate(dot(V, N));
    const float sigmaV = sqrt(0.5f * (sigmaSq.x + sigmaSq.y));
    return 0.02f + 0.98f * EvaluateBrunetonMeanFresnel(cosThetaV, sigmaV);
}

[shader("fragment")]
float4 OceanSurfaceFS(OceanSurfaceVaryings input) : SV_Target
{
    // Evaluate persistent foam before the cascade loop
    const bool persistent = View.PersistentFoamParameters.x > 0.5f;
    const float storedFoam = persistent ? SamplePersistentFoam(input.SurfaceXZ) : 0.0f;
    const float thresholdBias = View.PersistentFoamParameters.y * storedFoam;

    const float2 ddxXz = ddx(input.SurfaceXZ);
    const float2 ddyXz = ddy(input.SurfaceXZ);

    // Fused surface derivatives and whitecaps coverage
    SurfaceSample surface;
    float breaking;
    SampleSurfaceAndWhitecaps(input.SurfaceXZ, ddxXz, ddyXz, input.WorldPosition.y, thresholdBias, surface, breaking);

    // Candidate foam coverage for early-out gating
    const float maxResidualArea = persistent ? saturate(max(0.0f, View.FoamResidualParameters.x) * storedFoam) : 0.0f;
    const float candidateFoam = max(breaking, maxResidualArea);

    bool needFoamSampling;
    if (OCEAN_USE_WAVE_OPS && OCEAN_WAVE_SIZE > 0u)
    {
        needFoamSampling = WaveActiveAnyTrue(candidateFoam > 1.0e-4f);
    }
    else
    {
        needFoamSampling = (candidateFoam > 1.0e-4f);
    }

    FoamMaterial material = {};
    float whitecaps = breaking;

    if (needFoamSampling)
    {
        material = SampleFoamMaterial(input.SurfaceXZ);
        if (persistent && maxResidualArea > 0.0f)
        {
            const float residual = EvaluateResidualFoam(input.SurfaceXZ, ddxXz, ddyXz, storedFoam, material.Mask);
            whitecaps = max(breaking, residual);
        }
    }

    const float3 toCamera = normalize(View.CameraPositionTime.xyz - input.WorldPosition);
    const float3 toSun = View.SunDirectionSeabedDepth.xyz;

    // Construct local tangent frame on ocean surface
    const float3 tangentX = normalize(abs(surface.Normal.y) < 0.999f
        ? cross(float3(0.0f, 1.0f, 0.0f), surface.Normal)
        : float3(1.0f, 0.0f, 0.0f));
    const float3 tangentY = cross(surface.Normal, tangentX);

    // Underwater foam mixes bubble scattering into water column
    const float underwaterFoam = saturate(View.ShadingParameters.x * max(breaking, saturate(View.FoamResidualParameters.x * storedFoam)));

    // Sky reflection with LEADR-biased sampling
    float3 reflectDir = reflect(-toCamera, surface.Normal);
    reflectDir.y = max(0.01f, reflectDir.y);
    reflectDir = normalize(reflectDir);

    float3 skyRadiance = float3(0.0f);
    if (Frame.UseSkyTexture != 0u)
    {
        const float phi = atan2(reflectDir.z, reflectDir.x);
        const float theta = acos(clamp(reflectDir.y, -1.0f, 1.0f));
        const float2 skyUv = float2(phi * EON_INV_2PI + 0.5f, theta * EON_INV_PI);

        float2 ddxUv = ddx(skyUv);
        ddxUv.x -= round(ddxUv.x);
        float2 ddyUv = ddy(skyUv);
        ddyUv.x -= round(ddyUv.x);

        const float roughnessBias = 0.5f * log2(max(1.0f, (surface.SigmaSq.x + surface.SigmaSq.y) * 128.0f));
        skyRadiance = SampleEquirectangularSkyGrad(
            SkyDomeTexture,
            SkyDomeSampler,
            reflectDir,
            ddxUv,
            ddyUv,
            roughnessBias) * Frame.SkyIntensityScale;

        // Attenuate the sky reflection above the ambient sky threshold when analytical
        // Ross-Bruneton sun specular is active so the sun glint is not double-counted
        const float specularGain = max(0.0f, View.ShadingParameters.y);
        if (specularGain > 0.0f && any(Frame.SunRadianceAp1 > 0.0f))
        {
            const float avgSkyIrradiance = max(Frame.SkyShCoeffs[0].x, max(Frame.SkyShCoeffs[0].y, Frame.SkyShCoeffs[0].z));
            const float maxSkyLuminance = max(6.0f, avgSkyIrradiance * 2.5f);
            if (any(isnan(skyRadiance)) || any(isinf(skyRadiance)))
            {
                skyRadiance = float3(maxSkyLuminance);
            }
            else
            {
                const float skyLum = dot(skyRadiance, float3(0.2722287f, 0.6740818f, 0.0536895f));
                if (skyLum > maxSkyLuminance)
                {
                    skyRadiance *= (maxSkyLuminance / max(1e-4f, skyLum));
                }
            }
        }
    }
    else
    {
        skyRadiance = EvaluateSkyShIrradiance(reflectDir, Frame.SkyShCoeffs) * Frame.SkyIntensityScale;
    }

    if (any(isnan(skyRadiance)) || any(isinf(skyRadiance)) || all(skyRadiance <= float3(1e-4f)))
    {
        skyRadiance = lerp(float3(0.35f, 0.5f, 0.7f), float3(0.08f, 0.2f, 0.45f), saturate(reflectDir.y));
    }

    // Specular ocean BRDF and Bruneton effective Fresnel
    const float effectiveFresnel = EvaluateBrunetonFresnel(toCamera, surface.Normal, surface.SigmaSq);
    const float specularGain = max(0.0f, View.ShadingParameters.y);
    const float specularSun = EvaluateOceanSpecular(
        toSun,
        toCamera,
        surface.Normal,
        tangentX,
        tangentY,
        surface.SigmaSq) * specularGain;
    const float3 sunSpecularRadiance = Frame.SunRadianceAp1 * specularSun;

    const float tUf = 1.0f - underwaterFoam;
    const float tUf2 = tUf * tUf;
    const float tUf5 = tUf2 * tUf2 * tUf;
    const float specularMask = tUf5 * (1.0f - whitecaps);

    // Water column attenuation and optical depth
    const float depth = max(0.1f, View.SunDirectionSeabedDepth.w);
    const float3 waterExtinction = float3(0.22f, 0.045f, 0.015f);
    const float viewPathLength = depth / max(0.01f, toCamera.y);
    const float3 viewAttenuation = exp(-waterExtinction * viewPathLength);
    const float viewTransmission = dot(viewAttenuation, float3(0.2126f, 0.7152f, 0.0722f));
    float waterOpacity = saturate(1.0f - viewTransmission);
    waterOpacity = saturate(lerp(waterOpacity, 1.0f, smoothstep(1500.0f, 3000.0f, length(input.WorldPosition.xz - View.CameraPositionTime.xz))));

    // Directional forward crest SSS and water body scattering in AP1
    const float dSunCam = saturate(dot(-toSun, toCamera));
    const float dSunCam2 = dSunCam * dSunCam;
    const float forwardScatter = dSunCam2 * dSunCam2;
    const float crestFactor = saturate(max(0.0f, surface.DisplacementY) * 0.8f + 0.1f);
    const float3 clampedSunRadiance = min(Frame.SunRadianceAp1, float3(12.0f));
    const float3 crestSss = clampedSunRadiance * Frame.WaterScatterColorAp1 * (forwardScatter * crestFactor * Frame.SssStrength);
    const float3 skyZenithIrradiance = EvaluateSkyShIrradiance(float3(0.0f, 1.0f, 0.0f), Frame.SkyShCoeffs) * Frame.SkyIntensityScale;
    const float3 bodyScatter = Frame.WaterScatterColorAp1 * (saturate(toSun.y) * Frame.SunRadianceAp1 * 0.04f + skyZenithIrradiance * 0.08f);

    const float3 underwaterColor = float3(0.05f, 0.14f, 0.16f);
    const float3 waterSubsurface = lerp(bodyScatter, underwaterColor * Frame.WaterScatterColorAp1, underwaterFoam) + crestSss;

    // Combined water surface radiance
    const float3 waterRadiance = lerp(waterSubsurface * waterOpacity, skyRadiance, effectiveFresnel) + sunSpecularRadiance * specularMask;

    // Portsmouth et al. (2024) EON rough diffuse foam shading gated by wave early-out
    float3 foamRadiance = float3(0.0f);
    bool needFoamLighting;
    if (OCEAN_USE_WAVE_OPS && OCEAN_WAVE_SIZE > 0u)
    {
        needFoamLighting = WaveActiveAnyTrue(whitecaps > 1.0e-4f);
    }
    else
    {
        needFoamLighting = (whitecaps > 1.0e-4f);
    }

    if (needFoamLighting)
    {
        const float foamRoughness = material.Roughness;
        const float3 foamAlbedo = material.Albedo;
        const float3 foamNormal = normalize(surface.Normal + View.FoamTextureParameters.y * float3(material.NormalXY.x, 0.0f, material.NormalXY.y));

        const float3 foamDirect = EvaluateEonDirect(foamAlbedo, foamRoughness, foamNormal, toCamera, toSun) * Frame.SunRadianceAp1;
        const float3 skyIrradiance = EvaluateSkyShIrradiance(foamNormal, Frame.SkyShCoeffs) * Frame.SkyIntensityScale;
        const float3 foamIbl = EvaluateEonIbl(foamAlbedo, foamRoughness, foamNormal, toCamera, skyIrradiance);
        foamRadiance = foamDirect + foamIbl;
    }

    // Unclamped AP1 scene-linear radiance
    const float3 finalColor = lerp(waterRadiance, foamRadiance, whitecaps);
    const float surfaceAlpha = saturate(lerp(waterOpacity, 1.0f, effectiveFresnel) + whitecaps);
    return float4(finalColor, surfaceAlpha);
}

public struct OceanSurfaceGBufferOutput
{
    public float4 GBufferNormalSigma : SV_Target0;
    public float4 GBufferFoamSss : SV_Target1;
    public float4 GBufferMotionFoamAlbedo : SV_Target2;
};

[shader("fragment")]
OceanSurfaceGBufferOutput OceanSurfaceGBufferFS(OceanSurfaceVaryings input)
{
    const bool persistent = View.PersistentFoamParameters.x > 0.5f;
    const float storedFoam = persistent ? SamplePersistentFoam(input.SurfaceXZ) : 0.0f;
    const float thresholdBias = View.PersistentFoamParameters.y * storedFoam;

    const float2 ddxXz = ddx(input.SurfaceXZ);
    const float2 ddyXz = ddy(input.SurfaceXZ);

    SurfaceSample surface;
    float breaking;
    SampleSurfaceAndWhitecaps(input.SurfaceXZ, ddxXz, ddyXz, input.WorldPosition.y, thresholdBias, surface, breaking);

    const float maxResidualArea = persistent ? saturate(max(0.0f, View.FoamResidualParameters.x) * storedFoam) : 0.0f;
    const float candidateFoam = max(breaking, maxResidualArea);

    bool needFoamSampling;
    if (OCEAN_USE_WAVE_OPS && OCEAN_WAVE_SIZE > 0u)
    {
        needFoamSampling = WaveActiveAnyTrue(candidateFoam > 1.0e-4f);
    }
    else
    {
        needFoamSampling = (candidateFoam > 1.0e-4f);
    }

    FoamMaterial material = {};
    float whitecaps = breaking;

    if (needFoamSampling)
    {
        material = SampleFoamMaterial(input.SurfaceXZ);
        if (persistent && maxResidualArea > 0.0f)
        {
            const float residual = EvaluateResidualFoam(input.SurfaceXZ, ddxXz, ddyXz, storedFoam, material.Mask);
            whitecaps = max(breaking, residual);
        }
    }

    const float underwaterFoam = saturate(View.ShadingParameters.x * max(breaking, saturate(View.FoamResidualParameters.x * storedFoam)));

    const float2 currNdc = (input.ClipPosition.xy / max(1e-5f, input.ClipPosition.w)) - Frame.JitterOffsetNdc;
    const float2 prevNdc = (input.PrevClipPosition.xy / max(1e-5f, input.PrevClipPosition.w)) - Frame.PrevJitterOffsetNdc;
    const float2 motionUv = (currNdc - prevNdc) * float2(0.5f, 0.5f);

    const float foamLum = (View.FoamTextureTint.y > 1e-4f) ? (material.Albedo.y / View.FoamTextureTint.y) : material.Albedo.y;

    OceanSurfaceGBufferOutput output;
    output.GBufferNormalSigma = float4(surface.Normal.x, surface.Normal.z, surface.SigmaSq.x, surface.SigmaSq.y);
    output.GBufferFoamSss = float4(whitecaps, underwaterFoam, material.Roughness, foamLum);
    output.GBufferMotionFoamAlbedo = float4(motionUv.x, motionUv.y, material.NormalXY.x, material.NormalXY.y);
    return output;
}
