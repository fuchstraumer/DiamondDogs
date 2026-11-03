implementing VolumeTiledForwardShading;
__include VtfConfig;
__include VtfTypes;
__include VtfBindings;

import ddCommon;

// Interleaves the low VTF_MORTON_BITS bits of each axis. Bit i of x goes to position 3i, bit i of y to
// 3i + 1, and bit i of z to 3i + 2.
// fix: the argument was an int3, and a light outside the root box gave a negative coordinate
internal uint CalculateMortonCode(in const uint3 quantized_coord, in const uint k)
{
    uint mortonCode = 0u;
    uint bitMask = 1u;
    uint bitShift = 0u;
    const uint kBits = (1u << k);

    while (bitMask < kBits)
    {
        mortonCode |= ((quantized_coord.x & bitMask) << (bitShift));
        mortonCode |= ((quantized_coord.y & bitMask) << (bitShift + 1u));
        mortonCode |= ((quantized_coord.z & bitMask) << (bitShift + 2u));
        bitMask <<= 1u;
        bitShift += 2u;
    }

    return mortonCode;
}

// Maps a view-space position into the root box and returns the quantized cell.
internal uint3 QuantizePosition(in const float3 position, in const AABB root, in const float3 inverseRange)
{
    // saturate to catch out-of-range positions or edge cases before they hit morton encoding
    const float3 normalized = saturate((position - root.Min) * inverseRange);
    return uint3(normalized * float(VTF_MORTON_COORD_SCALE));
}

// fix: every lane per wave would've read the same address, so we now load per-lane instead of round-tripping
// through groupshared memory. the barrier also sat behind a potential early-return, which webgpu complains about
// fix: another case of using SV_GroupIndex instead of SV_DispatchThreadID, which would have caused incorrect indexing.
[shader("compute")]
[NumThreads(VTF_MORTON_NUM_THREADS, 1, 1)]
public void VtfComputeMortonCodes(
    uint3 dispatchThreadID: SV_DispatchThreadID,
    uniform ParameterBlock<VtfMortonCodesResources> resources)
{
    const uint lightIndex = dispatchThreadID.x;

    const AABB rootAABB = resources.LightAABBs[0];
    // fix: use a max to handle potential zero extent on any axis, which would've lead to inf scale
    const float3 extent = max(rootAABB.Max - rootAABB.Min, float3(1.0e-6f));
    const float3 inverseRange = 1.0f / extent;

    if (lightIndex < resources.Counts.NumPointLights)
    {
        const float3 lightPos = resources.PointLights[lightIndex].PositionViewSpace.xyz;
        const uint3 cell = QuantizePosition(lightPos, rootAABB, inverseRange);
        resources.PointLightMortonCodes[lightIndex] = CalculateMortonCode(cell, VTF_MORTON_BITS);
        resources.PointLightIndices[lightIndex] = lightIndex;
    }

    if (lightIndex < resources.Counts.NumSpotLights)
    {
        const float3 lightPos = resources.SpotLights[lightIndex].PositionViewSpace.xyz;
        const uint3 cell = QuantizePosition(lightPos, rootAABB, inverseRange);
        resources.SpotLightMortonCodes[lightIndex] = CalculateMortonCode(cell, VTF_MORTON_BITS);
        resources.SpotLightIndices[lightIndex] = lightIndex;
    }
}
