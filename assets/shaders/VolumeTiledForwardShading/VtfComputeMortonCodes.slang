implementing VolumeTiledForwardShading;
__include VtfTypes;
__include VtfFunctions;

internal groupshared AABB gs_MortonAABB;
internal groupshared float3 gs_MortonAABBRange;

internal uint CalculateMortonCode(const in int3 quantized_coord, const in uint k)
{
    uint mortonCode = 0u;
    uint bitMask = 1u;
    uint bitShift = 0u;
    uint kBits = (1 << k);

    while (bitMask < kBits)
    {
        mortonCode |= ((quantized_coord.x & bitMask) << (bitShift));
        mortonCode |= ((quantized_coord.y & bitMask) << (bitShift + 1));
        mortonCode |= ((quantized_coord.z & bitMask) << (bitShift + 2));
        bitMask <<= 1;
        bitShift++;
    }

    return mortonCode;
}

// Set at construction time to scale the number of threads per thread group, then used to set indexing
[SpecializationConstant]
internal const uint NumThreadsPerGroup = 1024u;

[shader("compute")][NumThreads(NumThreadsPerGroup, 1, 1)]
void VtfComputeMortonCodes(uint localInvocationIndex : SV_GroupIndex,
                           uniform PointLight* pointLights,
                           uniform SpotLight* spotLights,
                           uniform uint* pointLightIndices,
                           uniform uint* spotLightIndices,
                           uniform uint* pointLightMortonCodes,
                           uniform uint* spotLightMortonCodes,
                           uniform LightCounts* lightCounts,
                           uniform AABB* lightAABBs)
{
    // (recall that SV_GroupIndex gives us index not just within thread group, but across all threads in the dispatch, so we can use it for indexing)
    static const uint kBitMortonCode = 10u;
    static const uint coordinateScale = (1u << kBitMortonCode) - 1u;

    if (localInvocationIndex >= lightCounts.NumPointLights && localInvocationIndex >= lightCounts.NumSpotLights)
    {
        return; // No work to do
    }

    // Wonder if it's more efficient to just have each lane retrieve the root AABB, vs using group memory....
    if (localInvocationIndex == 0)
    {
        gs_MortonAABB = lightAABBs[0];
        gs_MortonAABBRange = 1.0f / (gs_MortonAABB.Max - gs_MortonAABB.Min);
    }

    GroupMemoryBarrierWithGroupSync();

    if (localInvocationIndex < lightCounts.NumPointLights)
    {
        const float3 lightPos = pointLights[localInvocationIndex].PositionViewSpace.xyz;
        const float3 quantizedCoord = (lightPos - gs_MortonAABB.Min) * gs_MortonAABBRange * coordinateScale;
        pointLightMortonCodes[localInvocationIndex] = CalculateMortonCode(int3(quantizedCoord), kBitMortonCode);
        pointLightIndices[localInvocationIndex] = localInvocationIndex;
    }

    if (localInvocationIndex < lightCounts.NumSpotLights)
    {
        const float3 lightPos = spotLights[localInvocationIndex].PositionViewSpace.xyz;
        const float3 quantizedCoord = (lightPos - gs_MortonAABB.Min) * gs_MortonAABBRange * coordinateScale;
        spotLightMortonCodes[localInvocationIndex] = CalculateMortonCode(int3(quantizedCoord), kBitMortonCode);
        spotLightIndices[localInvocationIndex] = localInvocationIndex;
    }

}
