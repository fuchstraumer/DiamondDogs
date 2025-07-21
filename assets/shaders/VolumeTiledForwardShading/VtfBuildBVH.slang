implementing VolumeTiledForwardShading;
__include VtfTypes;

import ddCommon;

// BuildBVH entrypoint and functions, constructs BVH used for light binning and culling

internal static const uint NumLevelNodes[7] =
{
    1,
    32,
    1024,
    32768,
    1048576,
    33554432,
    1073741824
};

internal static const uint FirstNodeIndex[7] =
{
    0,
    1,
    33,
    1057,
    33825,
    1082401,
    34636833
};

internal static const uint NUM_THREADS_X = 32 * 16;
internal static const float FLT_MAX = 3.402823466e+38f;

// unfortunately need duplicated code here, as the light buffers are different structured types :'(
internal void BuildBottomPointLights(
    in const uint dispatchThreadIndex,
    in const uint numPointLights,
    in const uint numPointLightLevels,
    in const RWTexture2D<uint> pointLightIndices,
    in const StructuredBuffer<PointLight> pointLights,
    in const RWStructuredBuffer<AABB> pointLightBVH)
{
        // First, have each thread in the wave load a light and compute its AABB
    uint leafIdx = dispatchThreadIndex;
    uint lightIdx = 0;
    float3 minAABB = float3(FLT_MAX, FLT_MAX, FLT_MAX);
    float3 maxAABB = float3(-FLT_MAX, -FLT_MAX, -FLT_MAX);

    if (leafIdx < numPointLights)
    {
        lightIdx = pointLightIndices.Load(int2(leafIdx, 0));
        PointLight currLight = pointLights[lightIdx];
        minAABB = currLight.PositionViewSpace.xyz - currLight.Range.xxx;
        maxAABB = currLight.PositionViewSpace.xyz + currLight.Range.xxx;
    }

    // no longer need big pool of groupshared float4s for this! just wave operations <333
    float3 waveMinAABB = WaveActiveMin(minAABB);
    float3 waveMaxAABB = WaveActiveMax(maxAABB);

    // For first thread in each wave, update point light BVH
    if (WaveIsFirstLane())
    {
        uint numLevels = numPointLightLevels;
        uint nodeOffset = dispatchThreadIndex / WaveGetLaneCount();

        if (numLevels > 0 && nodeOffset < NumLevelNodes[numLevels - 1])
        {
            uint nodeIndex = FirstNodeIndex[numLevels - 1] + nodeOffset;
            pointLightBVH[nodeIndex].Min = waveMinAABB;
            pointLightBVH[nodeIndex].Max = waveMaxAABB;
        }
    }
}

internal void BuildBottomSpotLights(
    in const uint dispatchThreadIndex,
    in const uint numSpotLights,
    in const uint numSpotLightLevels,
    in const RWTexture2D<uint> spotLightIndices,
    in const StructuredBuffer<SpotLight> spotLights,
    in const RWStructuredBuffer<AABB> spotLightBVH)
{
    const uint waveSize = WaveGetLaneCount();
    uint leafIdx = dispatchThreadIndex;
    uint lightIdx = -1;

    float3 minAABB = float3(FLT_MAX, FLT_MAX, FLT_MAX);
    float3 maxAABB = float3(-FLT_MAX, -FLT_MAX, -FLT_MAX);

    if (leafIdx < numSpotLights)
    {
        lightIdx = spotLightIndices.Load(int2(leafIdx, 0));
        SpotLight currLight = spotLights[lightIdx];
        // This is technically incorrect and quite conservative: we're treating the spot light as a sphere for AABB calculation.
        // Ideally we could use the cone angles to compute a tighter AABB, but until that proves to be an issue we'll just use a sphere.
        // Also, spot lights aren't as common anyways, so hopefully this won't be a big deal.
        minAABB = currLight.PositionViewSpace.xyz - currLight.Range.xxx;
        maxAABB = currLight.PositionViewSpace.xyz + currLight.Range.xxx;
    }

    float3 waveMinAABB = WaveActiveMin(minAABB);
    float3 waveMaxAABB = WaveActiveMax(maxAABB);

    if (WaveIsFirstLane())
    {
        uint numLevels = numSpotLightLevels;
        uint nodeOffset = dispatchThreadIndex / waveSize;

        if (numLevels > 0 && nodeOffset < NumLevelNodes[numLevels - 1])
        {
            uint nodeIndex = FirstNodeIndex[numLevels - 1] + nodeOffset;
            spotLightBVH[nodeIndex].Min = waveMinAABB;
            spotLightBVH[nodeIndex].Max = waveMaxAABB;
        }
    }
}

internal void BuildBottomBVH(
    in const uint dispatchThreadIndex,
    in const LightCounts lightCounts,
    in const BVHParams bvhParams,
    in const RWTexture2D<uint> pointLightIndices,
    in const RWTexture2D<uint> spotLightIndices,
    in const StructuredBuffer<PointLight> pointLights,
    in const StructuredBuffer<SpotLight> spotLights,
    in const RWStructuredBuffer<AABB> pointLightBVH,
    in const RWStructuredBuffer<AABB> spotLightBVH)
{
    // Build lower BVH for point lights
    if (lightCounts.NumPointLights > 0)
    {
        BuildBottomPointLights(dispatchThreadIndex, lightCounts.NumPointLights, bvhParams.PointLightLevels, pointLightIndices, pointLights, pointLightBVH);
    }

    // Build lower BVH for spot lights
    if (lightCounts.NumSpotLights > 0)
    {
        BuildBottomSpotLights(dispatchThreadIndex, lightCounts.NumSpotLights, bvhParams.SpotLightLevels, spotLightIndices, spotLights, spotLightBVH);
    }
}

// This stage can be generic, since we just deal with the BVH/AABBs and not the actual light data. phew!
internal void BuildUpperImpl(
    in const uint dispatchThreadIndex,
    in const uint childLevel,
    in const uint numLevels,
    inout RWStructuredBuffer<AABB> lightBVH)
{

    float3 minAABB = float3(FLT_MAX, FLT_MAX, FLT_MAX);
    float3 maxAABB = float3(-FLT_MAX, -FLT_MAX, -FLT_MAX);

    if (childLevel < numLevels && dispatchThreadIndex < NumLevelNodes[childLevel])
    {
        uint childIndex = FirstNodeIndex[childLevel] + dispatchThreadIndex;
        minAABB = lightBVH[childIndex].Min;
        maxAABB = lightBVH[childIndex].Max;
    }

    float3 waveMinAABB = WaveActiveMin(minAABB);
    float3 waveMaxAABB = WaveActiveMax(maxAABB);

    if (WaveIsFirstLane())
    {
        uint nodeOffset = dispatchThreadIndex / WaveGetLaneCount();

        if (childLevel < numLevels && nodeOffset < NumLevelNodes[childLevel - 1])
        {
            uint nodeIndex = FirstNodeIndex[childLevel + 1] + nodeOffset;
            lightBVH[nodeIndex].Min = waveMinAABB;
            lightBVH[nodeIndex].Max = waveMaxAABB;
        }
    }
}

internal void BuildUpper(
    in const uint dispatchThreadIndex,
    in const uint childLevel,
    in const BVHParams bvhParams,
    in const LightCounts lightCounts,
    inout RWStructuredBuffer<AABB> pointLightBVH,
    inout RWStructuredBuffer<AABB> spotLightBVH)
{
    // Build upper BVH for point lights
    if (lightCounts.NumPointLights > 0)
    {
        BuildUpperImpl(dispatchThreadIndex, childLevel, bvhParams.PointLightLevels, pointLightBVH);
    }

    // Build upper BVH for spot lights
    if (lightCounts.NumSpotLights > 0)
    {
        BuildUpperImpl(dispatchThreadIndex, childLevel, bvhParams.SpotLightLevels, spotLightBVH);
    }
}

[SpecializationConstant]
const uint BuildStage = 0; // 0 for bottom, 1 for upper

struct ResourcePointers
{
    RWStructuredBuffer<AABB>* PointLightBVH;
    RWStructuredBuffer<AABB>* SpotLightBVH;
    StructuredBuffer<PointLight>* PointLights;
    StructuredBuffer<SpotLight>* SpotLights;
    BVHParams* BvhParams;
    LightCounts* LightCounts;
    RWTexture2D<uint>* PointLightIndices;
    RWTexture2D<uint>* SpotLightIndices;
}

// ChildLevel should automatically be made a push constant by the compiler
[shader("compute")][NumThreads(32*16, 1, 1)]
void BuildBVH(uint3 dispatchThreadID : SV_DispatchThreadID, uniform ResourcePointers resources, uniform uint childLevel)
{
    RWStructuredBuffer<AABB> pointLightBVH = *resources.PointLightBVH;
    RWStructuredBuffer<AABB> spotLightBVH = *resources.SpotLightBVH;
    const RWTexture2D<uint> pointLightIndices = *resources.PointLightIndices;
    const RWTexture2D<uint> spotLightIndices = *resources.SpotLightIndices;
    const StructuredBuffer<PointLight> pointLights = *resources.PointLights;
    const StructuredBuffer<SpotLight> spotLights = *resources.SpotLights;
    const BVHParams bvhParams = *resources.BvhParams;
    const LightCounts lightCounts = *resources.LightCounts;

    switch (BuildStage)
    {
        case 0:
            BuildBottomBVH(dispatchThreadID.x, lightCounts, bvhParams, pointLightIndices, spotLightIndices, pointLights, spotLights, pointLightBVH, spotLightBVH);
            break;
        case 1:
            BuildUpper(dispatchThreadID.x, childLevel, bvhParams, lightCounts, pointLightBVH, spotLightBVH);
            break;
    }
}