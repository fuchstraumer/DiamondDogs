implementing VolumeTiledForwardShading;
__include VtfConfig;
__include VtfTypes;
__include VtfFunctions;
__include VtfBindings;

import ddCommon;

// Draws the box of every cluster that a visible surface fell into.
// Originally, we used to use geometry shaders to make this work. Those suck and WebGPU doesn't even
// bother implementing them, so we had to come up with something else. Thankfully, we can just do
// some oldschool vertex-id stuff and read from a local constant array. easy.
//
// The topology is a line list rather than a triangle strip. The original strip drew two open strips of
// a box and left four edges out, because a strip cannot close a box without a restart. A line list
// draws all twelve edges, which is what a cluster overlay wants to show.
// todo: It would be nice, just for these kinds of debug shaders, to have an attribute that could tell
// clients this uses line lists. Expands raster state a bit, but makes debug shaders more robust and
// easier to just write. Something to contemplate.

public struct VtfDebugClustersVertexOutput
{
    float4 ClipPosition: SV_Position;
    float4 Color: COLOR0;
}

// The two ends of each of the twelve edges of a box. A corner index carries one bit for each axis:
// bit 0 selects x, bit 1 selects y, and bit 2 selects z. Min is 0 and Max is 1.
internal static const uint k_ClusterEdgeCorners[VTF_DEBUG_CLUSTER_VERTS] =
{
    // four edges along x
    0u, 1u,  2u, 3u,  4u, 5u,  6u, 7u,
    // four edges along y
    0u, 2u,  1u, 3u,  4u, 6u,  5u, 7u,
    // four edges along z
    0u, 4u,  1u, 5u,  2u, 6u,  3u, 7u
};

internal float3 GetBoxCorner(in const AABB box, in const uint corner)
{
    return float3(
        ((corner & 1u) != 0u) ? box.Max.x : box.Min.x,
        ((corner & 2u) != 0u) ? box.Max.y : box.Min.y,
        ((corner & 4u) != 0u) ? box.Max.z : box.Min.z);
}

[shader("vertex")]
public VtfDebugClustersVertexOutput VtfDebugClustersVS(
    uint vertexID: SV_VertexID,
    uniform ParameterBlock<VtfDebugClustersResources> resources)
{
    const uint clusterSlot = vertexID / VTF_DEBUG_CLUSTER_VERTS;
    const uint edgeVertex = vertexID % VTF_DEBUG_CLUSTER_VERTS;

    const uint clusterIndex = resources.UniqueClusters[clusterSlot];
    const AABB box = resources.ClusterAABBs[clusterIndex];

    const float3 corner = GetBoxCorner(box, k_ClusterEdgeCorners[edgeVertex]);

    VtfDebugClustersVertexOutput output;
    // The cluster box is already in view space, so only the projection remains. The GLSL carried its
    // own debug view and projection pair so that the overlay could be inspected from a second camera.
    // CommonMatrices holds both, and one matrix set is easier to keep in step than two.
    output.ClipPosition = mul(resources.Matrices.projectionMatrix, float4(corner, 1.0f));
    // The colour comes from the index, so this pass binds no colour buffer. VtfClusterSamples uses the
    // same function, so a cluster keeps one colour across both overlays.
    output.Color = float4(ClusterIndexToDebugColor(clusterIndex), 1.0f);

    return output;
}

[shader("fragment")]
public float4 VtfDebugClustersFS(VtfDebugClustersVertexOutput input): SV_Target
{
    // The overlay draws over the shaded image, so it stays faint.
    return float4(input.Color.rgb, 0.2f);
}
