implementing VolumeTiledForwardShading;
__include VtfConfig;
__include VtfTypes;

import LodestoneAttributes;
import ddCommon;

// One source of truth for each entry point's resources. Every ls_element_count expression names
// constants from VtfConfig; the cooker multiplies the count by the reflected element stride, so no
// byte size is ever written by hand, and a capacity cannot drift from the buffer that uses it.

public struct VtfUpdateLightsResources
{
    [ls_element_count("VTF_MAX_POINT_LIGHTS")]
    RWStructuredBuffer<PointLight> PointLights;
    [ls_element_count("VTF_MAX_SPOT_LIGHTS")]
    RWStructuredBuffer<SpotLight> SpotLights;
    [ls_element_count("VTF_MAX_DIRECTIONAL_LIGHTS")]
    RWStructuredBuffer<DirectionalLight> DirectionalLights;
    ConstantBuffer<LightCounts> Counts;
    ConstantBuffer<CommonMatrices> Matrices;
}

public struct VtfReduceLightAABBsResources
{
    ConstantBuffer<LightCounts> Counts;
    [ls_element_count("VTF_MAX_POINT_LIGHTS")]
    StructuredBuffer<PointLight> PointLights;
    [ls_element_count("VTF_MAX_SPOT_LIGHTS")]
    StructuredBuffer<SpotLight> SpotLights;
    // pass 0 writes one AABB per group; pass 1 reads them and writes one at index 0
    [ls_element_count("VTF_MAX_POINT_LIGHTS / VTF_REDUCE_LIGHTS_NUM_THREADS + 1")]
    RWStructuredBuffer<AABB> LightAABBs;
    ConstantBuffer<ReduceLightAABBParams> Params;
}

public struct VtfMortonCodesResources
{
    [ls_element_count("VTF_MAX_POINT_LIGHTS")]
    StructuredBuffer<PointLight> PointLights;
    [ls_element_count("VTF_MAX_SPOT_LIGHTS")]
    StructuredBuffer<SpotLight> SpotLights;
    [ls_element_count("VTF_MAX_POINT_LIGHTS")]
    RWStructuredBuffer<uint> PointLightIndices;
    [ls_element_count("VTF_MAX_SPOT_LIGHTS")]
    RWStructuredBuffer<uint> SpotLightIndices;
    [ls_element_count("VTF_MAX_POINT_LIGHTS")]
    RWStructuredBuffer<uint> PointLightMortonCodes;
    [ls_element_count("VTF_MAX_SPOT_LIGHTS")]
    RWStructuredBuffer<uint> SpotLightMortonCodes;
    ConstantBuffer<LightCounts> Counts;
    [ls_element_count("VTF_MAX_POINT_LIGHTS / VTF_REDUCE_LIGHTS_NUM_THREADS + 1")]
    StructuredBuffer<AABB> LightAABBs;
}

// Both sort passes and the radix sort share this block: keys are Morton codes, values are light indices.
public struct VtfSortResources
{
    [ls_element_count("VTF_MAX_POINT_LIGHTS")]
    StructuredBuffer<uint> InputKeys;
    [ls_element_count("VTF_MAX_POINT_LIGHTS")]
    StructuredBuffer<uint> InputValues;
    [ls_element_count("VTF_MAX_POINT_LIGHTS")]
    RWStructuredBuffer<uint> OutputKeys;
    [ls_element_count("VTF_MAX_POINT_LIGHTS")]
    RWStructuredBuffer<uint> OutputValues;
    // one partition per group, plus one end marker per sort group; pass 0 needs the most (bounded by
    // lights / radix chunk size). At 8192 lights pass 0 needs 32.
    [ls_element_count("VTF_MAX_POINT_LIGHTS / VTF_RADIX_SORT_NUM_THREADS + 2")]
    RWStructuredBuffer<uint> MergePathPartitions;
    ConstantBuffer<SortParams> Params;
}

public struct VtfBuildBvhResources
{
    // BVH holds the levels the light capacity needs: the sum of the powers of the branching factor.
    [ls_element_count("(VTF_MAX_POINT_LIGHTS * VTF_BVH_BRANCHING) / (VTF_BVH_BRANCHING - 1) + 1")]
    RWStructuredBuffer<AABB> PointLightBVH;
    [ls_element_count("(VTF_MAX_SPOT_LIGHTS * VTF_BVH_BRANCHING) / (VTF_BVH_BRANCHING - 1) + 1")]
    RWStructuredBuffer<AABB> SpotLightBVH;
    [ls_element_count("VTF_MAX_POINT_LIGHTS")]
    StructuredBuffer<PointLight> PointLights;
    [ls_element_count("VTF_MAX_SPOT_LIGHTS")]
    StructuredBuffer<SpotLight> SpotLights;
    ConstantBuffer<BVHParams> BvhParams;
    ConstantBuffer<LightCounts> Counts;
    [ls_element_count("VTF_MAX_POINT_LIGHTS")]
    StructuredBuffer<uint> PointLightIndices;
    [ls_element_count("VTF_MAX_SPOT_LIGHTS")]
    StructuredBuffer<uint> SpotLightIndices;
}

public struct VtfAssignLightsResources
{
    [ls_element_count("VTF_CLUSTER_COUNT")]
    StructuredBuffer<uint> UniqueClusters;
    [ls_element_count("VTF_CLUSTER_COUNT")]
    StructuredBuffer<AABB> ClusterAABBs;
    ConstantBuffer<BVHParams> BvhParams;
    ConstantBuffer<LightCounts> Counts;
    [ls_element_count("VTF_MAX_POINT_LIGHTS")]
    StructuredBuffer<PointLight> PointLights;
    [ls_element_count("VTF_MAX_SPOT_LIGHTS")]
    StructuredBuffer<SpotLight> SpotLights;
    [ls_element_count("(VTF_MAX_POINT_LIGHTS * VTF_BVH_BRANCHING) / (VTF_BVH_BRANCHING - 1) + 1")]
    StructuredBuffer<AABB> PointLightBVH;
    [ls_element_count("(VTF_MAX_SPOT_LIGHTS * VTF_BVH_BRANCHING) / (VTF_BVH_BRANCHING - 1) + 1")]
    StructuredBuffer<AABB> SpotLightBVH;
    [ls_element_count("VTF_MAX_POINT_LIGHTS")]
    StructuredBuffer<uint> PointLightIndices;
    [ls_element_count("VTF_MAX_SPOT_LIGHTS")]
    StructuredBuffer<uint> SpotLightIndices;
    // worst case: every cluster holds a full light list
    [ls_element_count("VTF_CLUSTER_COUNT * VTF_MAX_LIGHTS_PER_CLUSTER")]
    RWStructuredBuffer<uint> PointLightIndexList;
    [ls_element_count("VTF_CLUSTER_COUNT * VTF_MAX_LIGHTS_PER_CLUSTER")]
    RWStructuredBuffer<uint> SpotLightIndexList;
    // uint2 states the (offset, count) pair, so an offset can never be read as a count
    [ls_element_count("VTF_CLUSTER_COUNT")]
    RWStructuredBuffer<uint2> PointLightGrid;
    [ls_element_count("VTF_CLUSTER_COUNT")]
    RWStructuredBuffer<uint2> SpotLightGrid;
    // Atomic<uint>: WGSL has no atomic op on a plain element, so the type must say so; Slang emits array<atomic<u32>>
    [ls_element_count("1")]
    RWStructuredBuffer<Atomic<uint>> PointLightIndexCounter;
    [ls_element_count("1")]
    RWStructuredBuffer<Atomic<uint>> SpotLightIndexCounter;
}

public struct VtfClusterAABBsResources
{
    ConstantBuffer<ClusterData> Clusters;
    ConstantBuffer<CommonMatrices> Matrices;
    [ls_element_count("VTF_CLUSTER_COUNT")]
    RWStructuredBuffer<AABB> ClusterAABBs;
}

public struct VtfFindUniqueClustersResources
{
    // one uint per flag: WGSL has no 8-bit storage type
    [ls_element_count("VTF_CLUSTER_COUNT")]
    RWStructuredBuffer<uint> ClusterFlags;
    [ls_element_count("1")]
    RWStructuredBuffer<Atomic<uint>> UniqueClustersCounter;
    [ls_element_count("VTF_CLUSTER_COUNT")]
    RWStructuredBuffer<uint> UniqueClusters;
}

public struct VtfClusterIndirectArgsResources
{
    [ls_element_count("1")]
    StructuredBuffer<uint> UniqueClustersCounter;
    [ls_element_count("1")]
    RWStructuredBuffer<ComputeIndirectArgs> IndirectArgs;
    [ls_element_count("1")]
    RWStructuredBuffer<DrawIndirectArgs> DebugClustersIndirectArgs;
}

// Raster passes. Each former GLSL YAML resource group becomes a ParameterBlock, like the groups above.

// ClusterColors is gone: the colour holds nothing the cluster index does not, so ClusterIndexToDebugColor
// computes it and both passes drop the binding.
public struct VtfClusterSamplesResources
{
    ConstantBuffer<ClusterData> Clusters;
    ConstantBuffer<CommonMatrices> Matrices;
    [ls_element_count("VTF_CLUSTER_COUNT")]
    RWStructuredBuffer<uint> ClusterFlags;
}

public struct VtfDebugClustersResources
{
    ConstantBuffer<CommonMatrices> Matrices;
    [ls_element_count("VTF_CLUSTER_COUNT")]
    StructuredBuffer<uint> UniqueClusters;
    [ls_element_count("VTF_CLUSTER_COUNT")]
    StructuredBuffer<AABB> ClusterAABBs;
}

public struct VtfDebugLightsResources
{
    ConstantBuffer<CommonMatrices> Matrices;
    [ls_element_count("VTF_MAX_POINT_LIGHTS")]
    StructuredBuffer<PointLight> PointLights;
    [ls_element_count("VTF_MAX_SPOT_LIGHTS")]
    StructuredBuffer<SpotLight> SpotLights;
}

public struct VtfDebugTextureResources
{
    Texture2D<float4> DebugTexture;
    SamplerState DebugSampler;
}
