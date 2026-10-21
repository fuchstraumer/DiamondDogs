implementing VolumeTiledForwardShading;
__include VtfConfig;
__include VtfTypes;
__include VtfFunctions;
__include VtfBindings;

import ddCommon;

// Computes the view-space box of every cluster. This pass had no Slang counterpart, and it writes the
// ClusterAABBs buffer that VtfAssignLightsToClusters reads. Without it the light assignment had no
// producer, and ClusterData and the three functions in VtfFunctions.slang were unreachable.
//
// The grid is uniform on x and y, and exponential on z, so that each depth slice covers a constant
// ratio of view depth rather than a constant distance.

[shader("compute")]
[NumThreads(VTF_SMALL_ENTRYPOINT_NUM_THREADS, 1, 1)]
public void VtfComputeClusterAABBs(
    uint3 dispatchThreadID: SV_DispatchThreadID,
    uniform ParameterBlock<VtfClusterAABBsResources> resources)
{
    const uint clusterIndex1D = dispatchThreadID.x;
    const uint3 gridDim = resources.Clusters.GridDim;
    const uint clusterCount = gridDim.x * gridDim.y * gridDim.z;

    if (clusterIndex1D >= clusterCount)
    {
        return;
    }

    const uint3 cluster3D = GridIdxToGridCoordinate(clusterIndex1D, gridDim);

    // The near and far cut of this depth slice. Both normals point along +z, which is what the GLSL
    // wrote, and the sign of d carries the direction, so the two planes stay distinct.
    Plane nearPlane;
    nearPlane.N = float3(0.0f, 0.0f, 1.0f);
    nearPlane.d = -resources.Clusters.ViewNear * pow(abs(resources.Clusters.NearK), float(cluster3D.z));

    Plane farPlane;
    farPlane.N = float3(0.0f, 0.0f, 1.0f);
    farPlane.d = -resources.Clusters.ViewNear * pow(abs(resources.Clusters.NearK), float(cluster3D.z + 1u));

    // The two screen-space corners of this cluster column.
    const float2 clusterSize = float2(resources.Clusters.ClusterSizeInPixels);
    const float2 screenSize = float2(gridDim.xy) * clusterSize;

    float4 pMin = float4(float2(cluster3D.xy) * clusterSize, 1.0f, 1.0f);
    float4 pMax = float4(float2(cluster3D.xy + uint2(1u, 1u)) * clusterSize, 1.0f, 1.0f);

    // The GLSL called inverse(projection_matrix) inside ClipToView, so every one of the 1024 threads
    // inverted a 4x4 matrix twice. CommonMatrices already carries the inverse.
    const float4x4 inverseProjection = resources.Matrices.inverseProjectionMatrix;

    pMin = ScreenToView(pMin, screenSize, inverseProjection);
    pMax = ScreenToView(pMax, screenSize, inverseProjection);

    const float3 eye = float3(0.0f, 0.0f, 0.0f);

    float3 nearMin;
    float3 nearMax;
    float3 farMin;
    float3 farMax;

    IntersectLinePlane(eye, pMin.xyz, nearPlane, nearMin);
    IntersectLinePlane(eye, pMax.xyz, nearPlane, nearMax);
    IntersectLinePlane(eye, pMin.xyz, farPlane, farMin);
    IntersectLinePlane(eye, pMax.xyz, farPlane, farMax);

    const float3 aabbMin = min(nearMin, min(nearMax, min(farMin, farMax)));
    const float3 aabbMax = max(nearMin, max(nearMax, max(farMin, farMax)));

    resources.ClusterAABBs[clusterIndex1D].Min = aabbMin;
    resources.ClusterAABBs[clusterIndex1D].Max = aabbMax;
}
