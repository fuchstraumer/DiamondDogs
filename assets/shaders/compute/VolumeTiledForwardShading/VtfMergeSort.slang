implementing VolumeTiledForwardShading;
__include VtfConfig;
__include VtfTypes;
__include VtfBindings;

// Merge-path parallel merge sort. It joins the sorted chunks that the radix sort produced.
//
// Pass 0 finds where each thread group starts inside the two chunks it merges, and writes those
// offsets to MergePathPartitions. Pass 1 reads them and performs the merge.

internal groupshared uint gs_Keys[VTF_MERGE_VALUES_PER_GROUP];
internal groupshared uint gs_Values[VTF_MERGE_VALUES_PER_GROUP];

internal static const uint MERGE_SENTINEL_KEY = 0xFFFFFFFFu;

// <summary>
// Finds the merge path for two sorted sequences using shared memory.
// This version operates on data already loaded into group shared memory.
// </summary>
// <param name="arrayAStart">Starting index of first sorted sequence in shared memory</param>
// <param name="arrayACount">Number of elements in first sequence</param>
// <param name="arrayBStart">Starting index of second sorted sequence in shared memory</param>
// <param name="arrayBCount">Number of elements in second sequence</param>
// <param name="diagonal">Target diagonal in the merge matrix</param>
// <returns>Index where the merge path intersects array A</returns>
internal int MergePathSharedMemory(int arrayAStart, int arrayACount, int arrayBStart, int arrayBCount, int diagonal)
{
    int searchBegin = max(0, diagonal - arrayBCount);
    int searchEnd = min(diagonal, arrayACount);

    while (searchBegin < searchEnd)
    {
        int searchMid = (searchBegin + searchEnd) >> 1;

        uint keyFromA = gs_Keys[arrayAStart + searchMid];
        uint keyFromB = gs_Keys[arrayBStart + diagonal - 1 - searchMid];

        if (keyFromA < keyFromB)
        {
            searchBegin = searchMid + 1;
        }
        else
        {
            searchEnd = searchMid;
        }
    }

    return searchBegin;
}

// <summary>
// Finds the merge path for two sorted sequences using buffer reads.
// This version loads data directly from buffer resources.
// </summary>
internal int MergePathBuffer(
    StructuredBuffer<uint> inputKeys,
    int arrayAStart,
    int arrayACount,
    int arrayBStart,
    int arrayBCount,
    int diagonal)
{
    int searchBegin = max(0, diagonal - arrayBCount);
    int searchEnd = min(diagonal, arrayACount);

    while (searchBegin < searchEnd)
    {
        int searchMid = (searchBegin + searchEnd) >> 1;

        uint keyFromA = inputKeys[arrayAStart + searchMid];
        uint keyFromB = inputKeys[arrayBStart + diagonal - 1 - searchMid];

        if (keyFromA < keyFromB)
        {
            searchBegin = searchMid + 1;
        }
        else
        {
            searchEnd = searchMid;
        }
    }

    return searchBegin;
}

// Pass 0. One thread finds the merge path of one partition.
internal void MergePathPartitionsFn(
    ParameterBlock<VtfSortResources> resources,
    in const uint globalInvocationIndex)
{
    const uint chunkSize = resources.Params.ChunkSize;
    const uint numElements = resources.Params.NumElements;

    // num values to sort per sort group in shader (work groups)
    const uint numValuesPerSortGroup = min(chunkSize * 2u, numElements);

    // number of chunks to sort, followed by number of sort groups to sort all chunks we have
    const uint numChunks = (numElements + chunkSize - 1u) / chunkSize;
    const uint numSortGroups = max(numChunks / 2u, 1u);

    // total number of "partitions" per group, then we find index of work group thread is in
    const uint numPartitionsPerSortGroup =
        ((numValuesPerSortGroup + VTF_MERGE_VALUES_PER_GROUP - 1u) / VTF_MERGE_VALUES_PER_GROUP) + 1u;
    const uint sortGroup = globalInvocationIndex / numPartitionsPerSortGroup;
    const uint partitionInSortGroup = globalInvocationIndex % numPartitionsPerSortGroup;

    const uint globalPartition = (sortGroup * numPartitionsPerSortGroup) + partitionInSortGroup;
    const uint maxPartitions = numSortGroups * numPartitionsPerSortGroup;

    if (globalPartition < maxPartitions)
    {
        const int a0 = int(min(sortGroup * numValuesPerSortGroup, numElements));
        const int a1 = int(min(uint(a0) + chunkSize, numElements));
        const int aCount = a1 - a0;
        const int b0 = a1;
        const int b1 = int(min(uint(b0) + chunkSize, numElements));
        const int bCount = b1 - b0;
        // number of values to sort total in this sort group
        const int numValues = aCount + bCount;
        // diagonal in the merge matrix we're building
        const int diag = min(int(partitionInSortGroup * VTF_MERGE_VALUES_PER_GROUP), numValues);

        const int mergePath = MergePathBuffer(resources.InputKeys, a0, aCount, b0, bCount, diag);

        resources.MergePathPartitions[globalPartition] = uint(mergePath);
    }
}

// Walks one thread's run of the merged output.
//
// fix: the four loads before the loop, and the two inside it, read gs_Keys and gs_Values with no test
// against a1 or b1. The last iteration of every thread read one element past the end of its run, and
// a thread whose run ended at the end of shared memory read past the array. The reads take a sentinel
// when the side is exhausted, which is also what makes the `b0 >= b1` test below correct without the
// extra read.
internal void SerialMerge(
    ParameterBlock<VtfSortResources> resources,
    int a0, int a1, int b0, int b1, int diag, uint numValues, uint out0)
{
    uint aKey = (a0 < a1) ? gs_Keys[a0] : MERGE_SENTINEL_KEY;
    uint bKey = (b0 < b1) ? gs_Keys[b0] : MERGE_SENTINEL_KEY;
    uint aValue = (a0 < a1) ? gs_Values[a0] : 0u;
    uint bValue = (b0 < b1) ? gs_Values[b0] : 0u;

    [unroll]
    for (uint i = 0u; i < VTF_MERGE_VALUES_PER_THREAD && uint(diag) + i < numValues; ++i)
    {
        const uint outIndex = out0 + uint(diag) + i;

        if (b0 >= b1 || (a0 < a1 && aKey < bKey))
        {
            resources.OutputKeys[outIndex] = aKey;
            resources.OutputValues[outIndex] = aValue;
            ++a0;
            aKey = (a0 < a1) ? gs_Keys[a0] : MERGE_SENTINEL_KEY;
            aValue = (a0 < a1) ? gs_Values[a0] : 0u;
        }
        else
        {
            resources.OutputKeys[outIndex] = bKey;
            resources.OutputValues[outIndex] = bValue;
            ++b0;
            bKey = (b0 < b1) ? gs_Keys[b0] : MERGE_SENTINEL_KEY;
            bValue = (b0 < b1) ? gs_Values[b0] : 0u;
        }
    }
}

// Pass 1. One thread group merges one partition of one sort group.
internal void MergeSortPass(
    ParameterBlock<VtfSortResources> resources,
    in const uint3 workGroupID,
    in const uint localInvocationIndex)
{
    const uint chunkSize = resources.Params.ChunkSize;
    const uint numElements = resources.Params.NumElements;

    const uint numChunks = (numElements + chunkSize - 1u) / chunkSize;
    const uint numSortGroups = max(numChunks / 2u, 1u);
    // find values per group, thread groups per group, and partitions per group
    const uint numValuesPerSortGroup = min(chunkSize * 2u, numElements);
    const uint numThreadGroupsPerSortGroup =
        max((numValuesPerSortGroup + VTF_MERGE_VALUES_PER_GROUP - 1u) / VTF_MERGE_VALUES_PER_GROUP, 1u);
    const uint numPartitionsPerSortGroup = numThreadGroupsPerSortGroup + 1u;
    // use preceding information to find current sort-group and sub-partition of said group
    const uint sortGroup = workGroupID.x / numThreadGroupsPerSortGroup;
    const uint partitionVal = workGroupID.x % numThreadGroupsPerSortGroup;
    const uint globalPartition = (sortGroup * numPartitionsPerSortGroup) + partitionVal;

    // Load required keys about merge paths to take from the partition buffer that pass 0 wrote.
    const int mergePath0 = int(resources.MergePathPartitions[globalPartition]);
    const int mergePath1 = int(resources.MergePathPartitions[globalPartition + 1u]);
    const int diag0 = min(int(partitionVal * VTF_MERGE_VALUES_PER_GROUP), int(numValuesPerSortGroup));
    const int diag1 = min(int((partitionVal + 1u) * VTF_MERGE_VALUES_PER_GROUP), int(numValuesPerSortGroup));

    // Compute chunk ranges in current input set
    const int chunkOffsetA0 = int(min(sortGroup * numValuesPerSortGroup, numElements));
    const int chunkOffsetA1 = int(min(uint(chunkOffsetA0) + chunkSize, numElements));
    const int chunkSizeA = chunkOffsetA1 - chunkOffsetA0;

    const int chunkOffsetB0 = chunkOffsetA1;
    const int chunkOffsetB1 = int(min(uint(chunkOffsetB0) + chunkSize, numElements));
    const int chunkSizeB = chunkOffsetB1 - chunkOffsetB0;

    const uint numValues = uint(chunkSizeA + chunkSizeB);

    const int a0 = mergePath0;
    const int a1 = mergePath1;
    const int numA = min(a1 - a0, chunkSizeA);

    const int b0 = diag0 - mergePath0;
    const int b1 = diag1 - mergePath1;
    const int numB = min(b1 - b0, chunkSizeB);

    // diagonal for this particular thread
    const int diag = int(localInvocationIndex * VTF_MERGE_VALUES_PER_THREAD);

    // loading of keys and values into local memory
    [unroll]
    for (uint i = 0u; i < VTF_MERGE_VALUES_PER_THREAD; ++i)
    {
        const int slot = diag + int(i);
        const int a = a0 + slot;
        const int b = b0 + (a - a1);

        uint key = MERGE_SENTINEL_KEY;
        uint value = 0u;

        // fix: the load took a key for every slot, even a slot past the end of both chunks. The guard
        // keeps the read inside the input buffer, and the sentinel sorts the empty slot to the back.
        if (a < a1)
        {
            if (uint(chunkOffsetA0 + a) < numElements)
            {
                key = resources.InputKeys[chunkOffsetA0 + a];
                value = resources.InputValues[chunkOffsetA0 + a];
            }
        }
        else if (b < b1 && uint(chunkOffsetB0 + b) < numElements)
        {
            key = resources.InputKeys[chunkOffsetB0 + b];
            value = resources.InputValues[chunkOffsetB0 + b];
        }

        if (uint(slot) < VTF_MERGE_VALUES_PER_GROUP)
        {
            gs_Keys[slot] = key;
            gs_Values[slot] = value;
        }
    }

    GroupMemoryBarrierWithGroupSync();

    const int mergePath = MergePathSharedMemory(0, numA, numA, numB, diag);

    SerialMerge(
        resources,
        mergePath,
        numA,
        numA + diag - mergePath,
        numA + numB,
        diag0 + diag,
        numValues,
        uint(chunkOffsetA0));
}

[shader("compute")]
[NumThreads(VTF_MERGE_SORT_NUM_THREADS, 1, 1)]
public void VtfMergeSort(
    uint3 dispatchThreadID: SV_DispatchThreadID,
    uint3 workGroupID: SV_GroupID,
    uint localInvocationIndex: SV_GroupIndex,
    uniform ParameterBlock<VtfSortResources> resources)
{
    // VTF_SORT_PASS selects the pass. The old name was SortAlgorithm, which stated that the constant
    // picks between two sorting algorithms. It picks between two passes of one algorithm.
    if (VTF_SORT_PASS == 0u)
    {
        MergePathPartitionsFn(resources, dispatchThreadID.x);
    }
    else
    {
        MergeSortPass(resources, workGroupID, localInvocationIndex);
    }
}
