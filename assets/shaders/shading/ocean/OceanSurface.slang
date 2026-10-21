implementing OceanRender;

// Vertex stage of the ocean surface. Every cascade's displacement is added at the vertex: the slopes and foam are
// sampled again per pixel in OceanShading.slang, because the grid is far coarser than the finest cascade.

public struct OceanSurfaceVaryings
{
    public float4 Position : SV_Position;
    // displaced position, for view vectors and distance
    public float3 WorldPosition : WORLD_POSITION;
    // the undisplaced grid position: the textures are indexed by it, not by the displaced one
    public float2 SurfaceXZ : SURFACE_XZ;
};

[shader("vertex")]
OceanSurfaceVaryings OceanSurfaceVS(uint vertexId : SV_VertexID, uniform OceanGridParameters Grid)
{
    uint2 quad;
    const float2 corner = GridCorner(vertexId, Grid.Resolution, quad);
    const float2 surfaceXZ = Grid.Origin + (float2(quad) + corner) * (Grid.Size / float(Grid.Resolution));

    float3 displacement = float3(0.0f);
    [unroll]
    for (uint cascade = 0; cascade < OCEAN_CASCADE_COUNT; ++cascade)
    {
        const float3 uvw = float3(CascadeUv(surfaceXZ, cascade), float(cascade));
        displacement += Displacement.SampleLevel(OceanSampler, uvw, 0.0f).xyz * View.CascadeWeights[cascade];
    }

    OceanSurfaceVaryings output;
    output.WorldPosition = float3(surfaceXZ.x, 0.0f, surfaceXZ.y) + displacement;
    output.SurfaceXZ = surfaceXZ;
    output.Position = WorldToClip(output.WorldPosition);
    return output;
}
