implementing VolumeTiledForwardShading;
__include VtfTypes;
__include VtfFunctions;

import ddCommon;

// This constant is also the size of the "chunks" we run the radix sort on
const uint RadixSortNumThreads = 256;

internal groupshared uint gs_RadixKeys[RadixSortNumThreads];
internal groupshared uint gs_RadixValues[RadixSortNumThreads];
// "E" in the psuedocode this comes from
internal groupshared uint gs_IsCurrentSortKeyBitFalse[RadixSortNumThreads];
// "F" in the psuedocode this comes from
internal groupshared uint gs_FalseKeyDestinationIdx[RadixSortNumThreads];
// "D" in the psuedocode this comes from
internal groupshared uint gs_KeyDestinationIdx[RadixSortNumThreads];
internal groupshared uint gs_NumFalseKeys; // num false keys per iteration
static const uint UINT_MAX = 0xFFFFFFFFu;
static const uint NumBits = 32u;

[shader("compute")][NumThreads(RadixSortNumThreads, 1, 1)]
void VtfRadixSort(
    uint3 dispatchThreadID : SV_DispatchThreadID,
    uint localInvocationIndex : SV_GroupIndex,
    const uint* InputKeys,
    const uint* InputValues,
    uint* OutputKeys,
    uint* OutputValues,
    uniform uint NumElements)
{
    // Initialize keys and values in shared memory
    gs_RadixKeys[localInvocationIndex] = dispatchThreadID.x < NumElements ? InputKeys[dispatchThreadID.x] : UINT_MAX;
    gs_RadixValues[localInvocationIndex] = dispatchThreadID.x < NumElements ? InputValues[dispatchThreadID.x] : UINT_MAX;

    // memory barrier to ensure all threads have written their keys and values
    GroupMemoryBarrier();

    for (uint b = 0; b < NumBits; ++b)
    {
        // Check to see if the key is false for this bit
        const uint isSortKeyFalse = (gs_RadixKeys[localInvocationIndex] & (1u << b)) == 0u ? 1u : 0u;
        gs_IsCurrentSortKeyBitFalse[localInvocationIndex] = isSortKeyFalse;
        GroupMemoryBarrier();
        gs_FalseKeyDestinationIdx[localInvocationIndex] = localInvocationIndex == 0u ? gs_IsCurrentSortKeyBitFalse[localInvocationIndex - 1u] : 0u;
        // memory barrier + group sync to ensure everyone is together for the next step
        GroupMemoryBarrierWithGroupSync();

        [[unroll]]
        // iterate using logarithmic stride, performing parallel prefix sum (scan step)
        for (uint i = 1u; i < RadixSortNumThreads; i <<= 1u)
        {
            uint currValue = gs_FalseKeyDestinationIdx[localInvocationIndex];
            uint prevValue = (localInvocationIndex >= i) ? gs_FalseKeyDestinationIdx[localInvocationIndex - i] : 0u;
            currValue += prevValue;
            GroupMemoryBarrier();
            gs_FalseKeyDestinationIdx[localInvocationIndex] = currValue;
            GroupMemoryBarrier();
        }

        if (localInvocationIndex == 0u)
        {
            const uint lastIdx = RadixSortNumThreads - 1u;
            gs_NumFalseKeys = gs_FalseKeyDestinationIdx[lastIdx] + gs_IsCurrentSortKeyBitFalse[lastIdx];
        }

        GroupMemoryBarrierWithGroupSync();

        // Now we know how many false keys there are, we can calculate the destination index for each key
        if (gs_IsCurrentSortKeyBitFalse[localInvocationIndex] == 1u)
        {
            // If the key is false, it goes to the front of the array
            gs_KeyDestinationIdx[localInvocationIndex] = gs_FalseKeyDestinationIdx[localInvocationIndex];
        }
        else
        {
            // If the key is true, it goes to the end of the array
            gs_KeyDestinationIdx[localInvocationIndex] = localInvocationIndex - gs_FalseKeyDestinationIdx[localInvocationIndex] + gs_NumFalseKeys;
        }

        const uint key = gs_RadixKeys[localInvocationIndex];
        const uint value = gs_RadixValues[localInvocationIndex];

        GroupMemoryBarrier();

        const uint destIdx = gs_KeyDestinationIdx[localInvocationIndex];
        gs_RadixKeys[destIdx] = key;
        gs_RadixValues[destIdx] = value;

        GroupMemoryBarrierWithGroupSync();
    }

    // Write the sorted keys and values back to the output arrays
    if (dispatchThreadID.x < NumElements)
    {
        OutputKeys[dispatchThreadID.x] = gs_RadixKeys[localInvocationIndex];
        OutputValues[dispatchThreadID.x] = gs_RadixValues[localInvocationIndex];
    }
}

// Same as above, but uses wave operations to increase performance and reduce shared memory reads/writes
// 3 barriers, and several times fewer shared memory ops (plus we don't use the gs_IsCurrentSortKeyBitFalse array or gs_NumFalseKeys)
[shader("compute")] [NumThreads(RadixSortNumThreads, 1, 1)]
void VtfRadixSort_WaveOps(
    uint localInvocationIndex: SV_GroupIndex,
    const uint *InputKeys,
    const uint *InputValues,
    uint *OutputKeys,
    uint *OutputValues,
    uniform uint NumElements)
{
    // Initialize keys and values in shared memory
    gs_RadixKeys[localInvocationIndex] = localInvocationIndex < NumElements ? InputKeys[localInvocationIndex] : UINT_MAX;
    gs_RadixValues[localInvocationIndex] = localInvocationIndex < NumElements ? InputValues[localInvocationIndex] : UINT_MAX;

    // memory barrier to ensure all threads have written their keys and values
    GroupMemoryBarrier();

    for (uint b = 0; b < NumBits; ++b)
    {
        // Check to see if the key is false for this bit
        const uint currKey = gs_RadixKeys[localInvocationIndex];
        const uint isSortKeyFalse = (currKey & (1u << b)) == 0u ? 1u : 0u;

        // prepare to do the parallel prefix sum, avoiding one of the shared memory arrays and a lot of shared memory reads/writes

        // for each lane, this will be like an index or count of how many false keys there are in the wave, up to the index of this lane
        // this is not uniform across the wave, it's per-lane!
        const uint waveFalsePrefix = WavePrefixSum(isSortKeyFalse);
        // this is the total count of false keys in the wave, which is the same for all lanes in the wave (unlike previous)
        const uint waveTotalFalse = WaveActiveSum(isSortKeyFalse);

        // current wave out of total waves in dispatch/group
        const uint waveIndex = localInvocationIndex / WaveGetLaneCount();

        // if this is the first lane, store the false count for this wave at the index of this wave
        if (WaveIsFirstLane())
        {
            gs_FalseKeyDestinationIdx[waveIndex] = waveTotalFalse;
        }

        GroupMemoryBarrierWithGroupSync();

        // get prefix sum of false keys for this wave, and total false keys across all waves
        uint offsetForCurrentWave = 0;
        uint totalNumFalseKeys = 0;

        const uint totalNumWaves = RadixSortNumThreads / WaveGetLaneCount();
        for (uint w = 0; w < totalNumWaves; ++w)
        {
            const uint currVal = gs_FalseKeyDestinationIdx[w];
            if (w < waveIndex)
            {
                offsetForCurrentWave += currVal;
            }
            totalNumFalseKeys += currVal;
        }

        // no need for a barrier, because we are only reading and barrier'd above after finishing our wave prefix sum

        uint destIdx = 0u;
        if (isSortKeyFalse == 1u)
        {
            // by using waveFalsePrefix, we get the destination offset for *this* lane in the current wave
            destIdx = offsetForCurrentWave + waveFalsePrefix;
        }
        else
        {
            // as part of radix sort, we move true keys after false keys. this idx
            const uint trueKeysBeforeThisLane = localInvocationIndex - (offsetForCurrentWave + waveFalsePrefix);
            destIdx = totalNumFalseKeys + trueKeysBeforeThisLane;
        }

        // reuse variable, we haven't touched the array since the start of the loop anyways
        const uint key = currKey;
        const uint value = gs_RadixValues[localInvocationIndex];

        // barrier to get everyone caught up and shared reads/writes done
        GroupMemoryBarrierWithGroupSync();

        gs_RadixKeys[destIdx] = key;
        gs_RadixValues[destIdx] = value;

        // and another barrier, to make sure all writes are done before the next iteration
        GroupMemoryBarrierWithGroupSync();
    }

    // Write the sorted keys and values back to the output arrays
    if (localInvocationIndex < NumElements)
    {
        OutputKeys[localInvocationIndex] = gs_RadixKeys[localInvocationIndex];
        OutputValues[localInvocationIndex] = gs_RadixValues[localInvocationIndex];
    }
}