implementing VolumeTiledForwardShading;
__include VtfConfig;
__include VtfTypes;
__include VtfFunctions;
__include VtfBindings;

import ddCommon;

// Marks every cluster that a visible surface falls into, and paints the cluster colour so that the
// grid can be seen. VtfFindUniqueClusters reads the flags that this pass writes, so the cluster
// pipeline has no input without it.
//
// The pass runs with the depth buffer already filled and depth writes off, so only the surfaces that
// survive the depth test mark a cluster. That is the whole point: a cluster that no visible surface
// touches costs nothing later.

// input reduced just to positions: mesh data used may contain more, but only position is needed
// clients will have to handle the striding themselves, since they're the ones who know the shape
// of their mesh data
public struct VtfClusterSamplesVertexInput
{
    float3 Position: POSITION;
}

public struct VtfClusterSamplesVertexOutput
{
    float4 ClipPosition: SV_Position;
    // View-space position. The depth slice comes from its z, so the fragment stage needs the view
    // space value and not the clip space one.
    float3 ViewPosition: TEXCOORD0;
}

[shader("vertex")]
public VtfClusterSamplesVertexOutput VtfClusterSamplesVS(
    VtfClusterSamplesVertexInput input,
    uniform ParameterBlock<VtfClusterSamplesResources> resources)
{
    const float4 viewPosition = mul(resources.Matrices.viewMatrix, float4(input.Position, 1.0f));

    VtfClusterSamplesVertexOutput output;
    output.ClipPosition = mul(resources.Matrices.projectionMatrix, viewPosition);
    output.ViewPosition = viewPosition.xyz;

    return output;
}

[shader("fragment")]
public float4 VtfClusterSamplesFS(
    VtfClusterSamplesVertexOutput input,
    float4 fragCoord: SV_Position,
    uniform ParameterBlock<VtfClusterSamplesResources> resources): SV_Target
{
    const uint3 gridDim = resources.Clusters.GridDim;

    const uint3 cluster3D = ComputeClusterIndex3D(
        fragCoord.xy,
        input.ViewPosition.z,
        float2(resources.Clusters.ClusterSizeInPixels),
        resources.Clusters.ViewNear,
        resources.Clusters.LogGridDimY);

    // A surface at the very edge of the far slice can land one past the grid, and the depth slice is
    // a logarithm, so a value just outside the near plane can land far outside it. The clamp keeps the
    // write inside the flag buffer.
    const uint3 clamped = min(cluster3D, gridDim - uint3(1u, 1u, 1u));
    const uint clusterIndex = GridCoordinateToGridIdx(clamped, gridDim);

    resources.ClusterFlags[clusterIndex] = 1u;

    // cluster color written out is used for debugging. The hash replaces the ClusterColors buffer that
    // the GLSL bound, because the index already carries everything the colour said.
    return float4(ClusterIndexToDebugColor(clusterIndex), 0.2f);
}
