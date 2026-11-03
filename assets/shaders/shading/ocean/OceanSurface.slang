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
    public float4 ClipPosition : CLIP_POS;
    public float4 PrevClipPosition : PREV_CLIP_POS;
};

[shader("vertex")]
OceanSurfaceVaryings OceanSurfaceVS(
    uint vertexId : SV_VertexID,
    uint instanceId : SV_InstanceID,
    uniform OceanGridParameters Grid)
{
    float2 surfaceXZ = float2(0.0f);
    float quadSize = 1.0f;

    if (Frame.UseCdlod != 0u)
    {
        const OceanPatchVertex patchVertex = PatchVertices.Load(vertexId);
        const OceanCdlodPatch patch = SelectedPatches.Load(instanceId);

        const float2 gridCoord = patchVertex.GridUv * 64.0f;
        const uint2 iCoord = uint2(round(gridCoord));
        const uint edgeMask = (patch.LodAndEdgeMask >> 4u) & 0xFu;

        const float2 unmorphedWorldXz = patch.WorldOriginXz + patchVertex.GridUv * patch.PatchScale;
        const float vertexDistance = length(float3(unmorphedWorldXz.x, 0.0f, unmorphedWorldXz.y) - View.CameraPositionTime.xyz);
        const float rawMorph = saturate((vertexDistance - patch.MorphStart) * patch.InvMorphRange);
        const float morphFactor = smoothstep(0.0f, 1.0f, rawMorph);

        const float2 morphOffset = frac(gridCoord * 0.5f) * 2.0f;
        float2 morphWeight = float2(morphFactor, morphFactor);
        if (((edgeMask & 1u) != 0u && iCoord.x == 0u) || ((edgeMask & 2u) != 0u && iCoord.x >= 64u))
        {
            morphWeight.y = 1.0f;
        }

        if (((edgeMask & 4u) != 0u && iCoord.y == 0u) || ((edgeMask & 8u) != 0u && iCoord.y >= 64u))
        {
            morphWeight.x = 1.0f;
        }

        const float2 morphedGridCoord = gridCoord - morphOffset * morphWeight;
        const float2 morphedUv = morphedGridCoord / 64.0f;

        surfaceXZ = patch.WorldOriginXz + morphedUv * patch.PatchScale;
        quadSize = patch.PatchScale / 64.0f;
    }
    else
    {
        uint2 quad;
        const float2 corner = GridCorner(vertexId, Grid.Resolution, quad);
        uint2 vertex = quad + uint2(corner);
        const int2 tile = RingTile(Grid.Ring, instanceId);

        // on an edge that borders the coarser next ring, snap onto that ring's vertices: the triangles there collapse,
        // and the two rings share every edge vertex
        bool onOuterEdge = false;

        if (Grid.OuterResolution < Grid.Resolution)
        {
            const uint ratio = Grid.Resolution / Grid.OuterResolution;
            const int r = int(Grid.Ring);
            const bool onOuterX = (tile.x == r && vertex.x == Grid.Resolution) || (tile.x == -r && vertex.x == 0u);
            const bool onOuterZ = (tile.y == r && vertex.y == Grid.Resolution) || (tile.y == -r && vertex.y == 0u);
            onOuterEdge = onOuterX || onOuterZ;

            if (onOuterX)
            {
                vertex.y = (vertex.y / ratio) * ratio;
            }

            if (onOuterZ)
            {
                vertex.x = (vertex.x / ratio) * ratio;
            }
        }

        surfaceXZ = Grid.Origin + (float2(tile) + float2(vertex) / float(Grid.Resolution)) * Grid.Size;

        const uint lodResolution = onOuterEdge ? Grid.OuterResolution : Grid.Resolution;
        quadSize = Grid.Size / float(lodResolution);
    }

    const float distance = length(surfaceXZ - View.CameraPositionTime.xz);

    float3 displacement = float3(0.0f);
    [unroll]
    for (uint cascade = 0; cascade < OCEAN_CASCADE_COUNT; ++cascade)
    {
        const float weight = CascadeDistanceWeight(cascade, distance);
        if (weight > 0.0f)
        {
            const float3 uvw = float3(CascadeUv(surfaceXZ, cascade), float(cascade));
            const float3 sampled = Displacement.SampleLevel(OceanSampler, uvw, CascadeLod(cascade, quadSize)).xyz;
            const float2 horizontal = CascadeToWorld(sampled.xz, cascade);
            displacement += float3(horizontal.x, sampled.y, horizontal.y) * weight;
        }
    }

    const float earthRadius = 6371000.0f;
    const float horizDistSq = dot(surfaceXZ - View.CameraPositionTime.xz, surfaceXZ - View.CameraPositionTime.xz);
    displacement.y -= 0.5f * horizDistSq / earthRadius;

    OceanSurfaceVaryings output;
    output.WorldPosition = float3(surfaceXZ.x, 0.0f, surfaceXZ.y) + displacement;
    output.SurfaceXZ = surfaceXZ;
    output.Position = WorldToClip(output.WorldPosition);
    output.ClipPosition = output.Position;
    output.PrevClipPosition = PrevWorldToClip(output.WorldPosition);
    return output;
}
