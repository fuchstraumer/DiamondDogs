implementing OceanRender;

public static const uint OCEAN_CASCADE_COUNT = 4u;

// Per frame. std140: every member is a whole float4
public struct OceanViewParameters
{
    // the rows of the view-projection matrix, so the host and the shader can't disagree on a matrix layout rule
    public float4 ViewProjectionRows[4];
    // xyz: camera position in world space, w: simulation time in seconds
    public float4 CameraPositionTime;
    // the patch size of each cascade in meters, as the simulation was created with
    public float4 LengthScales;
    // per-cascade weights on displacement and slopes: 1 is the simulation as computed, 0 turns a cascade off (A/B)
    public float4 CascadeWeights;
    // xyz: unit vector towards the sun, w: seabed depth in meters (positive down)
    public float4 SunDirectionSeabedDepth;
    // x: the cascade fade distance in patch lengths. Cascade c fades out between x * LengthScales[c] and twice that,
    // so waves far smaller than a pixel stop shimmering. y: texels along an edge of mip 0 of the textures. zw unused
    public float4 LodParameters;
};

// Per draw, as push constants. The surface is square tiles of Size meters, each a grid of Resolution x Resolution
// quads, in rings around a center tile: ring 0 is the center tile, ring r the 8r tiles around ring r - 1. One draw
// covers one ring, one instance per tile. The seabed draws a single tile (Ring 0)
public struct OceanGridParameters
{
    // world-space x and z of the center tile's first corner
    public float2 Origin;
    // world-space width of one tile, in meters
    public float Size;
    // quads along a tile edge, in this ring
    public uint Resolution;
    public uint Ring;
    // quads along a tile edge in ring Ring + 1. When it is coarser, this ring's outer edge vertices snap onto its
    // vertices, so the rings meet without cracks
    public uint OuterResolution;
};

// The tile of ring `ring` that instance `instance` draws, in tiles from the center tile
internal int2 RingTile(uint ring, uint instance)
{
    if (ring == 0u)
    {
        return int2(0, 0);
    }

    // walk the ring's four sides, 2 * ring tiles each, counter-clockwise from the (-ring, -ring) corner
    const int r = int(ring);
    const int side = int(instance) / (2 * r);
    const int t = int(instance) % (2 * r);
    switch (side)
    {
    case 0:
        return int2(-r + t, -r);
    case 1:
        return int2(r, -r + t);
    case 2:
        return int2(r - t, r);
    default:
        return int2(-r, r - t);
    }
}

// A cascade's weight at a horizontal distance from the camera: the A/B weight, faded out once its waves are far
// below a pixel
internal float CascadeDistanceWeight(uint cascade, float distance)
{
    const float fadeStart = View.LodParameters.x * View.LengthScales[cascade];
    return View.CascadeWeights[cascade] * (1.0f - smoothstep(fadeStart, 2.0f * fadeStart, distance));
}

// The mip of cascade `cascade` whose texels are `spacing` meters wide: the level that holds no wave finer than a grid
// with that spacing can carry
internal float CascadeLod(uint cascade, float spacing)
{
    const float texelSize = View.LengthScales[cascade] / View.LodParameters.y;
    return max(0.0f, log2(spacing / texelSize));
}

ConstantBuffer<OceanViewParameters> View;
// written by OceanResolveCS: (x, y, z) displacement in meters, w the Jacobian of the horizontal displacement
Texture2DArray<float4> Displacement;
// written by OceanResolveCS: (dHeight/dx, dHeight/dz, dDx/dx, dDz/dz)
Texture2DArray<float4> Derivatives;
// linear, repeat: each cascade tiles with its own patch size
SamplerState OceanSampler;

internal float4 WorldToClip(float3 world_position)
{
    const float4 position = float4(world_position, 1.0f);
    return float4(dot(View.ViewProjectionRows[0], position), dot(View.ViewProjectionRows[1], position),
                  dot(View.ViewProjectionRows[2], position), dot(View.ViewProjectionRows[3], position));
}

internal float2 CascadeUv(float2 world_xz, uint cascade)
{
    return world_xz / View.LengthScales[cascade];
}

// Six vertices per quad, two triangles, no vertex or index buffer. Returns the quad corner in [0, 1]^2
internal float2 GridCorner(uint vertex_id, uint resolution, out uint2 quad)
{
    const uint quadIndex = vertex_id / 6u;
    quad = uint2(quadIndex % resolution, quadIndex / resolution);
    const float2 corners[6] = { float2(0, 0), float2(0, 1), float2(1, 0), float2(1, 0), float2(0, 1), float2(1, 1) };
    return corners[vertex_id % 6u];
}
