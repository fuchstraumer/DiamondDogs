implementing OceanRender;

// Seabed shading module.
// Renders the flat seabed beneath the ocean using wet sand textures (Albedo, ORD, Normal),
// Portsmouth et al. (2024) EON diffuse BRDF, Beer-Lambert water column absorption,
// and water column in-scattering in AP1 space.

public struct SeabedVaryings
{
    public float4 Position : SV_Position;
    public float3 WorldPosition : WORLD_POSITION;
};

[shader("vertex")]
SeabedVaryings SeabedVS(uint vertexId : SV_VertexID, uniform OceanGridParameters Grid)
{
    uint2 quad;
    const float2 corner = GridCorner(vertexId, Grid.Resolution, quad);
    const float2 xz = Grid.Origin + (float2(quad) + corner) * (Grid.Size / float(Grid.Resolution));

    SeabedVaryings output;
    output.WorldPosition = float3(xz.x, -View.SunDirectionSeabedDepth.w, xz.y);
    output.Position = WorldToClip(output.WorldPosition);
    return output;
}

[shader("fragment")]
float4 SeabedFS(SeabedVaryings input) : SV_Target
{
    const float3 toCamera = normalize(View.CameraPositionTime.xyz - input.WorldPosition);
    const float3 toSun = View.SunDirectionSeabedDepth.xyz;
    const float depth = max(0.1f, View.SunDirectionSeabedDepth.w);

    const float2 seabedUv = input.WorldPosition.xz * 0.25f;
    const float3 albedoSample = SeabedAlbedoTexture.Sample(SeabedSampler, seabedUv).xyz;
    const float4 ordSample = SeabedOrdTexture.Sample(SeabedSampler, seabedUv);
    const float2 normalXy = SeabedNormalTexture.Sample(SeabedSampler, seabedUv).xy * 2.0f - 1.0f;

    const float normalZ = sqrt(max(0.0f, 1.0f - dot(normalXy, normalXy)));
    const float3 normal = normalize(float3(normalXy.x, normalZ, normalXy.y));

    const float ao = ordSample.r;
    const float roughness = clamp(ordSample.g * Frame.SeabedRoughnessScale, 0.0f, 1.0f);
    const float3 albedo = saturate(albedoSample * Frame.SeabedTintAp1);

    // Beer-Lambert absorption through water column in AP1
    const float3 waterExtinction = float3(0.22f, 0.045f, 0.015f);
    const float sunPathLength = depth / max(0.01f, toSun.y);
    const float viewPathLength = depth / max(0.01f, toCamera.y);

    const float3 sunAttenuation = exp(-waterExtinction * sunPathLength);
    const float3 viewAttenuation = exp(-waterExtinction * viewPathLength);

    // Direct lighting with Portsmouth EON
    const float3 incidentSun = Frame.SunRadianceAp1 * sunAttenuation;
    const float3 directDiffuse = EvaluateEonDirect(albedo, roughness, normal, toCamera, toSun) * incidentSun;

    // Ambient sky irradiance with spherical harmonics and depth attenuation
    const float3 skyIrradiance = EvaluateSkyShIrradiance(normal, Frame.SkyShCoeffs)
        * exp(-waterExtinction * depth) * Frame.SkyIntensityScale;
    const float3 iblDiffuse = EvaluateEonIbl(albedo, roughness, normal, toCamera, skyIrradiance);

    const float3 seabedRadiance = (directDiffuse + iblDiffuse) * ao;
    const float3 attenuatedSeabed = seabedRadiance * viewAttenuation;

    // In-scattering from water column
    const float3 inscatter = Frame.WaterScatterColorAp1 * (float3(1.0f) - viewAttenuation)
        * (Frame.SunRadianceAp1 * saturate(toSun.y) * 0.15f + skyIrradiance * 0.25f);

    const float3 finalRadiance = attenuatedSeabed + inscatter;
    return float4(finalRadiance, 1.0f);
}
