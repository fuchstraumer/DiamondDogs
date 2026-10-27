implementing OceanRender;

public static const uint OCEAN_CASCADE_COUNT = 5u;

// Per frame. std140: every member is a whole float4
public struct OceanViewParameters
{
    // the rows of the view-projection matrix, so the host and the shader can't disagree on a matrix layout rule
    public float4 ViewProjectionRows[4];
    // xyz: camera position in world space, w: simulation time in seconds
    public float4 CameraPositionTime;
    // the patch size of each cascade in meters, as the simulation was created with. Each per-cascade array holds
    // eight cascades in two float4s, read through the helpers below
    public float4 LengthScales[2];
    // per-cascade weights on displacement and slopes: 1 is the simulation as computed, 0 turns a cascade off (A/B)
    public float4 CascadeWeights[2];
    // per-cascade weights on the foam: 1 is Dupuy's coverage as computed, 0 leaves the cascade at rest. They scale the
    // cascade's deviation from rest, so a small patch can be kept from stamping its tiling onto the foam
    public float4 FoamWeights[2];
    // xyz: unit vector towards the sun, w: seabed depth in meters (positive down)
    public float4 SunDirectionSeabedDepth;
    // x: the cascade fade distance in patch lengths. Cascade c fades out between x * LengthScales[c] and twice that,
    // so waves far smaller than a pixel stop shimmering. y: texels along an edge of mip 0 of the textures.
    // z: 1 rotates and offsets each cascade's lattice (tiling A/B, see CascadeUv), 0 samples them all aligned. w unused
    public float4 LodParameters;
    // x: WhitecapThreshold (breaking trigger [0, 1] -> [-0.2, 0.2]), y: FoamAmount (coverage scale),
    // z: FoamSpread (footprint dispersion), w: FoamContrast (falloff gamma)
    public float4 WhitecapParameters;
    // the persistent foam (OceanFoam.slang). x: 1 lets it raise the breaking threshold, 0 leaves the whitecaps as
    // they were (A/B). y: the threshold raise at foam 1, in Jacobian units. z, w: texel size of clipmap levels 0 and
    // 1 in meters
    public float4 PersistentFoamParameters;
    // xy: the foam's drift-frame offset in world meters (world = frame + xy). zw: the world xz the clipmap windows
    // are centered on (the orbit target)
    public float4 FoamDrift;
    // the residual foam, drawn on its own where the stored foam outlasts the breaking (OceanFoam.slang).
    // x: amount, y: dissolve softness, z: noise cell size of the first octave in meters, w unused
    public float4 FoamResidualParameters;
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

internal float ViewLengthScale(uint cascade)
{
    return View.LengthScales[cascade >> 2u][cascade & 3u];
}

internal float ViewCascadeWeight(uint cascade)
{
    return View.CascadeWeights[cascade >> 2u][cascade & 3u];
}

internal float ViewFoamWeight(uint cascade)
{
    return View.FoamWeights[cascade >> 2u][cascade & 3u];
}

// the angle of cascade `cascade`'s lattice in radians: the golden angle per cascade, so no two lattices line up. 0 when off
internal float CascadeAngle(uint cascade)
{
    return View.LodParameters.z > 0.5f ? float(cascade) * 2.39996323f : 0.0f;
}

// the fixed uv offset of cascade `cascade`, so the lattices don't share an origin either. 0 when off
internal float2 CascadeOffset(uint cascade)
{
    return View.LodParameters.z > 0.5f ? frac(float(cascade) * float2(0.7548777f, 0.5698403f)) : float2(0.0f);
}

// R_c * v, with R_c the rotation by CascadeAngle (the identity when off)
internal float2 WorldToCascade(float2 v, uint cascade)
{
    float sine;
    float cosine;
    sincos(CascadeAngle(cascade), sine, cosine);
    return float2(cosine * v.x - sine * v.y, sine * v.x + cosine * v.y);
}

// R_c^T * v: a vector sampled in the cascade's rotated frame (a displacement or a gradient), back in the world frame
internal float2 CascadeToWorld(float2 v, uint cascade)
{
    float sine;
    float cosine;
    sincos(CascadeAngle(cascade), sine, cosine);
    return float2(cosine * v.x + sine * v.y, -sine * v.x + cosine * v.y);
}

// (dDx/dx, dDz/dz) of the cascade's frame, in the world frame. R^T A R would need the off-diagonal term, which is not
// stored, so this takes the diagonal of it as if the off-diagonal were zero
internal float2 CascadeStretchToWorld(float2 stretch, uint cascade)
{
    float sine;
    float cosine;
    sincos(CascadeAngle(cascade), sine, cosine);
    const float cosineSq = cosine * cosine;
    const float sineSq = sine * sine;
    return float2(cosineSq * stretch.x + sineSq * stretch.y, sineSq * stretch.x + cosineSq * stretch.y);
}

// A cascade's weight at a horizontal distance from the camera: the A/B weight, faded out once its waves are far
// below a pixel
internal float CascadeDistanceWeight(uint cascade, float distance)
{
    const float fadeStart = View.LodParameters.x * ViewLengthScale(cascade);
    return ViewCascadeWeight(cascade) * (1.0f - smoothstep(fadeStart, 2.0f * fadeStart, distance));
}

// The mip of cascade `cascade` whose texels are `spacing` meters wide: the level that holds no wave finer than a grid
// with that spacing can carry
internal float CascadeLod(uint cascade, float spacing)
{
    const float texelSize = ViewLengthScale(cascade) / View.LodParameters.y;
    return max(0.0f, log2(spacing / texelSize));
}

ConstantBuffer<OceanViewParameters> View;
// written by OceanResolveCS: (x, y, z) displacement in meters, w the Jacobian of the horizontal displacement
Texture2DArray<float4> Displacement;
// written by OceanResolveCS: (dHeight/dx, dHeight/dz, dDx/dx, dDz/dz)
Texture2DArray<float4> Derivatives;
// linear, repeat: each cascade tiles with its own patch size
SamplerState OceanSampler;
// written by OceanResolveCS: k_i^2 for whitecap coverage evaluation. The mean is in Displacement.w
// [IFFT_SIZE, IFFT_SIZE, CASCADE_COUNT]
[format("r16f")]
Texture2DArray<float> Whitecaps;
// the persistent foam clipmap, one layer per level (OceanFoam.slang): OceanFoamStepCS writes it as storage, the
// fragment stage samples it. Both bind the same image
[format("r16f")]
RWTexture2DArray<float> FoamClipmapStorage;
[format("r16f")]
Texture2DArray<float> FoamClipmap;

internal float4 WorldToClip(float3 world_position)
{
    const float4 position = float4(world_position, 1.0f);
    return float4(dot(View.ViewProjectionRows[0], position), dot(View.ViewProjectionRows[1], position),
                  dot(View.ViewProjectionRows[2], position), dot(View.ViewProjectionRows[3], position));
}

internal float2 CascadeUv(float2 world_xz, uint cascade)
{
    return WorldToCascade(world_xz, cascade) / ViewLengthScale(cascade) + CascadeOffset(cascade);
}

// Six vertices per quad, two triangles, no vertex or index buffer. Returns the quad corner in [0, 1]^2
internal float2 GridCorner(uint vertex_id, uint resolution, out uint2 quad)
{
    const uint quadIndex = vertex_id / 6u;
    quad = uint2(quadIndex % resolution, quadIndex / resolution);
    const float2 corners[6] = { float2(0, 0), float2(0, 1), float2(1, 0), float2(1, 0), float2(0, 1), float2(1, 1) };
    return corners[vertex_id % 6u];
}
