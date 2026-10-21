implementing VolumeTiledForwardShading;
__include VtfConfig;
__include VtfTypes;
__include VtfBindings;

// A least-significant-bit radix sort over one chunk. One thread group sorts
// VTF_RADIX_SORT_NUM_THREADS keys, and the merge sort joins the chunks afterwards.
//
// The split step for each bit is: mark the keys whose bit is zero, take the exclusive scan of those
// marks, and send each key to its scanned slot. The names below follow the pseudocode the author
// worked from.

internal groupshared uint gs_RadixKeys[VTF_RADIX_SORT_NUM_THREADS];
internal groupshared uint gs_RadixValues[VTF_RADIX_SORT_NUM_THREADS];
// "E" in the psuedocode this comes from
internal groupshared uint gs_IsCurrentSortKeyBitFalse[VTF_RADIX_SORT_NUM_THREADS];
// "F" in the psuedocode this comes from
internal groupshared uint gs_FalseKeyDestinationIdx[VTF_RADIX_SORT_NUM_THREADS];
 // num false keys per iteration
internal groupshared uint gs_NumFalseKeys;
// One entry for each wave, for the wave path only.
internal groupshared uint gs_WaveFalseCounts[VTF_RADIX_SORT_MAX_WAVES];
internal static const uint UINT_MAX = 0xFFFFFFFFu;

// fix: Load/Store chunk only used the group index, not multiplying by the num_threads to get
// the right strided location to access. oops. this meant nothing worked as callers expected
internal uint GetChunkBase(in const uint groupIndex)
{
    return groupIndex * VTF_RADIX_SORT_NUM_THREADS;
}

// Loads the chunk into shared memory. A slot past the end takes the largest key, so it sorts to the
// back and never displaces a real key.
internal void LoadChunk(
    ParameterBlock<VtfSortResources> resources,
    in const uint chunkBase,
    in const uint localInvocationIndex)
{
    const uint element = chunkBase + localInvocationIndex;
    const bool inRange = element < resources.Params.NumElements;

    gs_RadixKeys[localInvocationIndex] = inRange ? resources.InputKeys[element] : UINT_MAX;
    gs_RadixValues[localInvocationIndex] = inRange ? resources.InputValues[element] : UINT_MAX;

    GroupMemoryBarrierWithGroupSync();
}

internal void StoreChunk(
    ParameterBlock<VtfSortResources> resources,
    in const uint chunkBase,
    in const uint localInvocationIndex)
{
    const uint element = chunkBase + localInvocationIndex;
    if (element < resources.Params.NumElements)
    {
        resources.OutputKeys[element] = gs_RadixKeys[localInvocationIndex];
        resources.OutputValues[element] = gs_RadixValues[localInvocationIndex];
    }
}

// Shared-memory path, and the control arm. It uses no wave intrinsic.
internal void RadixSortSharedMemory(
    ParameterBlock<VtfSortResources> resources,
    in const uint chunkBase,
    in const uint localInvocationIndex)
{
    LoadChunk(resources, chunkBase, localInvocationIndex);

    for (uint b = 0u; b < VTF_RADIX_BITS; ++b)
    {
        const uint currKey = gs_RadixKeys[localInvocationIndex];
        const uint currValue = gs_RadixValues[localInvocationIndex];
        const uint isSortKeyFalse = ((currKey >> b) & 1u) == 0u ? 1u : 0u;

        gs_IsCurrentSortKeyBitFalse[localInvocationIndex] = isSortKeyFalse;

        // fix: the next line reads the mark of the previous lane, and the marks were written without a
        // barrier between the write and the read. The read could take the value of the previous bit.
        GroupMemoryBarrierWithGroupSync();

        // Shifting the marks right by one turns the inclusive scan below into an exclusive scan.
        //
        // fix: this line read `localInvocationIndex == 0u ? gs_IsCurrentSortKeyBitFalse[idx - 1u] : 0u`.
        // The two arms were the wrong way round, so lane 0 read index -1 and every other lane took a
        // zero. The scan produced zero for every lane, and the sort did not sort.
        gs_FalseKeyDestinationIdx[localInvocationIndex] =
            (localInvocationIndex == 0u) ? 0u : gs_IsCurrentSortKeyBitFalse[localInvocationIndex - 1u];

        GroupMemoryBarrierWithGroupSync();

        // Hillis-Steele inclusive scan.
        //
        // fix: the loop held one barrier, between the read and the write. The next iteration then read
        // a slot that the previous iteration had written with no barrier in between. A scan step needs
        // one barrier after the reads and one after the writes.
        for (uint stride = 1u; stride < VTF_RADIX_SORT_NUM_THREADS; stride <<= 1u)
        {
            const uint prevValue = (localInvocationIndex >= stride)
                ? gs_FalseKeyDestinationIdx[localInvocationIndex - stride]
                : 0u;

            GroupMemoryBarrierWithGroupSync();

            gs_FalseKeyDestinationIdx[localInvocationIndex] += prevValue;

            GroupMemoryBarrierWithGroupSync();
        }

        if (localInvocationIndex == 0u)
        {
            const uint lastIdx = VTF_RADIX_SORT_NUM_THREADS - 1u;
            gs_NumFalseKeys = gs_FalseKeyDestinationIdx[lastIdx] + gs_IsCurrentSortKeyBitFalse[lastIdx];
        }

        // fix: every lane read gs_NumFalseKeys directly after lane 0 wrote it, with no barrier. A lane
        // could take the total of the previous bit and scatter its key to the wrong half.
        GroupMemoryBarrierWithGroupSync();

        // A key whose bit is zero goes to its scanned slot. A key whose bit is one goes after every
        // zero key, at the count of the one keys that come before it.
        uint destIdx;
        if (isSortKeyFalse == 1u)
        {
            destIdx = gs_FalseKeyDestinationIdx[localInvocationIndex];
        }
        else
        {
            const uint trueKeysBefore = localInvocationIndex - gs_FalseKeyDestinationIdx[localInvocationIndex];
            destIdx = gs_NumFalseKeys + trueKeysBefore;
        }

        GroupMemoryBarrierWithGroupSync();

        gs_RadixKeys[destIdx] = currKey;
        gs_RadixValues[destIdx] = currValue;

        GroupMemoryBarrierWithGroupSync();
    }

    StoreChunk(resources, chunkBase, localInvocationIndex);
}

// Same as above, but uses wave operations to increase performance and reduce shared memory reads/writes
// Way fewer barriers - 3 barriers per bit tested for a total of 96 sync points, vs 192 sync points in the non-wave version
// also probably about 1kb reduction in shared memory usage, so hopefully higher occupancy
internal void RadixSortWaveOps(
    ParameterBlock<VtfSortResources> resources,
    in const uint chunkBase,
    in const uint localInvocationIndex)
{
    LoadChunk(resources, chunkBase, localInvocationIndex);

    const uint laneCount = WaveGetLaneCount();
    const uint waveIndex = localInvocationIndex / laneCount;
    // The min() keeps the loop below inside gs_WaveFalseCounts even on a device whose wave is narrower
    // than VTF_MIN_WAVE_SIZE.
    const uint totalNumWaves = min(
        (VTF_RADIX_SORT_NUM_THREADS + laneCount - 1u) / laneCount,
        VTF_RADIX_SORT_MAX_WAVES);

    for (uint b = 0u; b < VTF_RADIX_BITS; ++b)
    {
        const uint currKey = gs_RadixKeys[localInvocationIndex];
        const uint currValue = gs_RadixValues[localInvocationIndex];
        const uint isSortKeyFalse = ((currKey >> b) & 1u) == 0u ? 1u : 0u;

        // for each lane, this will be like an index or count of how many false keys there are in the wave, up to the index of this lane
        // this is not uniform across the wave, it's per-lane!
        const uint waveFalsePrefix = WavePrefixSum(isSortKeyFalse);
        // this is the total count of false keys in the wave, which is the same for all lanes in the wave (unlike previous)
        const uint waveTotalFalse = WaveActiveSum(isSortKeyFalse);

        // Here WaveIsFirstLane is the right test, because the value belongs to the wave and not to the
        // group. Compare VtfAssignLightsToClustersBVH, where it stood for thread 0 of the group.
        if (WaveIsFirstLane() && waveIndex < VTF_RADIX_SORT_MAX_WAVES)
        {
            gs_WaveFalseCounts[waveIndex] = waveTotalFalse;
        }

        GroupMemoryBarrierWithGroupSync();

        uint offsetForCurrentWave = 0u;
        uint totalNumFalseKeys = 0u;
        for (uint w = 0u; w < totalNumWaves; ++w)
        {
            const uint currVal = gs_WaveFalseCounts[w];
            if (w < waveIndex)
            {
                offsetForCurrentWave += currVal;
            }
            totalNumFalseKeys += currVal;
        }

        uint destIdx;
        if (isSortKeyFalse == 1u)
        {
            // by using waveFalsePrefix, we get the destination offset for *this* lane in the current wave
            destIdx = offsetForCurrentWave + waveFalsePrefix;
        }
        else
        {
            const uint trueKeysBeforeThisLane = localInvocationIndex - (offsetForCurrentWave + waveFalsePrefix);
            destIdx = totalNumFalseKeys + trueKeysBeforeThisLane;
        }

        // barrier to get everyone caught up and shared reads/writes done
        GroupMemoryBarrierWithGroupSync();

        gs_RadixKeys[destIdx] = currKey;
        gs_RadixValues[destIdx] = currValue;

        // and another barrier, to make sure all writes are done before the next iteration
        GroupMemoryBarrierWithGroupSync();
    }

    StoreChunk(resources, chunkBase, localInvocationIndex);
}

// The entry point that the pipeline uses. VTF_USE_WAVE_OPS selects the arm, and the constant folds at
// link time, so only one arm reaches the emitted text.
[shader("compute")]
[NumThreads(VTF_RADIX_SORT_NUM_THREADS, 1, 1)]
public void VtfRadixSort(
    uint3 workGroupID: SV_GroupID,
    uint localInvocationIndex: SV_GroupIndex,
    uniform ParameterBlock<VtfSortResources> resources)
{
    const uint chunkBase = GetChunkBase(workGroupID.x);

    if (VTF_USE_WAVE_OPS)
    {
        RadixSortWaveOps(resources, chunkBase, localInvocationIndex);
    }
    else
    {
        RadixSortSharedMemory(resources, chunkBase, localInvocationIndex);
    }
}

// The reference. It always takes the shared-memory arm, so it is inert to both wave axes and
// ExpectedAxisInfluence can assert that. Two implementations that agree beat one implementation
// trusted twice, and the Python test compares this entry point against VtfRadixSort.
[shader("compute")]
[NumThreads(VTF_RADIX_SORT_NUM_THREADS, 1, 1)]
public void VtfRadixSort_NoWaveOps(
    uint3 workGroupID: SV_GroupID,
    uint localInvocationIndex: SV_GroupIndex,
    uniform ParameterBlock<VtfSortResources> resources)
{
    RadixSortSharedMemory(resources, GetChunkBase(workGroupID.x), localInvocationIndex);
}
