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
};

// Per draw, as push constants: the surface is a regular grid of Resolution x Resolution quads
public struct OceanGridParameters
{
    // world-space x and z of the grid's first corner
    public float2 Origin;
    // world-space width of the grid, in meters
    public float Size;
    public uint Resolution;
};

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
