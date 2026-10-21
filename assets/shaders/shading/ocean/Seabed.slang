implementing OceanRender;

// A flat seabed under the surface rings (one tile, Ring 0, as large as all of them), at View.SunDirectionSeabedDepth.w meters below sea level. Placeholder
// procedural sand until the CC0 sand textures are in: a texture binding replaces SeabedAlbedo.

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

internal float3 SeabedAlbedo(float2 xz)
{
    // two crossed sine ripples, like sand under shallow water
    const float ripples = 0.5f + 0.25f * sin(xz.x * 1.7f + sin(xz.y * 0.9f) * 2.0f) + 0.25f * sin(xz.y * 3.1f + xz.x * 0.4f);
    return lerp(float3(0.45f, 0.38f, 0.26f), float3(0.62f, 0.55f, 0.4f), ripples);
}

[shader("fragment")]
float4 SeabedFS(SeabedVaryings input) : SV_Target
{
    const float3 toSun = View.SunDirectionSeabedDepth.xyz;
    const float lambert = saturate(toSun.y) * 0.8f + 0.2f;
    return float4(SeabedAlbedo(input.WorldPosition.xz) * lambert, 1.0f);
}
