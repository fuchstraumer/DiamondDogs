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
OceanSurfaceVaryings OceanSurfaceVS(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID, uniform OceanGridParameters Grid)
{
    uint2 quad;
    const float2 corner = GridCorner(vertexId, Grid.Resolution, quad);
    uint2 vertex = quad + uint2(corner);
    const int2 tile = RingTile(Grid.Ring, instanceId);

    // on an edge that borders the coarser next ring, snap onto that ring's vertices: the triangles there collapse,
    // and the two rings share every edge vertex
    if (Grid.OuterResolution < Grid.Resolution)
    {
        const uint ratio = Grid.Resolution / Grid.OuterResolution;
        const int r = int(Grid.Ring);
        const bool onOuterX = (tile.x == r && vertex.x == Grid.Resolution) || (tile.x == -r && vertex.x == 0u);
        const bool onOuterZ = (tile.y == r && vertex.y == Grid.Resolution) || (tile.y == -r && vertex.y == 0u);
        if (onOuterX)
        {
            vertex.y = (vertex.y / ratio) * ratio;
        }

        if (onOuterZ)
        {
            vertex.x = (vertex.x / ratio) * ratio;
        }
    }

    // a power-of-two ratio between rings keeps these exact, so shared edge vertices land on the same position
    const float2 surfaceXZ = Grid.Origin + (float2(tile) + float2(vertex) / float(Grid.Resolution)) * Grid.Size;
    const float distance = length(surfaceXZ - View.CameraPositionTime.xz);

    float3 displacement = float3(0.0f);
    [unroll]
    for (uint cascade = 0; cascade < OCEAN_CASCADE_COUNT; ++cascade)
    {
        const float weight = CascadeDistanceWeight(cascade, distance);
        if (weight > 0.0f)
        {
            const float3 uvw = float3(CascadeUv(surfaceXZ, cascade), float(cascade));
            displacement += Displacement.SampleLevel(OceanSampler, uvw, 0.0f).xyz * weight;
        }
    }

    OceanSurfaceVaryings output;
    output.WorldPosition = float3(surfaceXZ.x, 0.0f, surfaceXZ.y) + displacement;
    output.SurfaceXZ = surfaceXZ;
    output.Position = WorldToClip(output.WorldPosition);
    return output;
}
