implementing VolumeTiledForwardShading;
__include VtfTypes;

[SpecializationConstant]
const uint SortAlgorithm = 0;
[SpecializationConstant]
const uint NumThreadsX = 256;
[SpecializationConstant]
const uint NumValuesPerThread = 8;

static const uint NumValuesPerThreadGroup = NumThreadsX * NumValuesPerThread;

internal groupshared uint gs_Keys[NumValuesPerThreadGroup];
internal groupshared uint gs_Values[NumValuesPerThreadGroup];

struct MergeSortParams
{
    uint NumElements;
    uint ChunkSize;
}

struct MergeSortResources
{
    uint* InputKeys;
    uint* InputValues;
    uint* OutputKeys;
    uint* OutputValues;
    uint* MergePathPartitions;
    MergeSortParams* Params;
}

/// <summary>
/// Finds the merge path for two sorted sequences using shared memory.
/// This version operates on data already loaded into group shared memory.
/// </summary>
/// <param name="arrayAStart">Starting index of first sorted sequence in shared memory</param>
/// <param name="arrayACount">Number of elements in first sequence</param>
/// <param name="arrayBStart">Starting index of second sorted sequence in shared memory</param>
/// <param name="arrayBCount">Number of elements in second sequence</param>
/// <param name="diagonal">Target diagonal in the merge matrix</param>
/// <returns>Index where the merge path intersects array A</returns>
int MergePathSharedMemory(int arrayAStart, int arrayACount, int arrayBStart, int arrayBCount, int diagonal)
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

/// <summary>
/// Finds the merge path for two sorted sequences using buffer reads.
/// This version loads data directly from buffer resources.
/// </summary>
/// <param name="inputKeys">Buffer containing the sorted key values</param>
/// <param name="arrayAStart">Starting index of first sorted sequence in the buffer</param>
/// <param name="arrayACount">Number of elements in first sequence</param>
/// <param name="arrayBStart">Starting index of second sorted sequence in the buffer</param>
/// <param name="arrayBCount">Number of elements in second sequence</param>
/// <param name="diagonal">Target diagonal in the merge matrix</param>
/// <returns>Index where the merge path intersects array A</returns>
int MergePathBuffer(uniform uint* inputKeys, int arrayAStart, int arrayACount, int arrayBStart, int arrayBCount, int diagonal)
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

/// <summary>
/// General merge path function that can use either shared memory or buffer access
/// </summary>
int MergePath(uniform MergeSortResources resources, int a0, int aCount, int b0, int bCount, int diag, bool useSharedMem)
{
    if (useSharedMem)
    {
        return MergePathSharedMemory(a0, aCount, b0, bCount, diag);
    }
    else
    {
        return MergePathBuffer(resources.InputKeys, a0, aCount, b0, bCount, diag);
    }
}

internal void MergePathPartitionsFn(
    in const uint globalInvocationIndex,
    uniform MergeSortResources resources)
{
    uint chunkSize = resources.Params.ChunkSize;
    uint numElements = resources.Params.NumElements;
    
    // num values to sort per sort group in shader (work groups)
    uint numValuesPerSortGroup = min(chunkSize * 2, numElements);

    // number of chunks to sort, followed by number of sort groups to sort all chunks we have
    uint numChunks = uint(ceil(numElements / float(chunkSize)));
    uint numSortGroups = numChunks / 2;

    // total number of "partitions" per group, then we find index of work group thread is in
    uint numPartitionsPerSortGroup = uint(ceil(numValuesPerSortGroup / float(NumValuesPerThreadGroup))) + 1;
    uint sortGroup = globalInvocationIndex / numPartitionsPerSortGroup;
    uint partitionInSortGroup = globalInvocationIndex % numPartitionsPerSortGroup;

    // get index of partition
    uint globalPartition = (sortGroup * numPartitionsPerSortGroup) + partitionInSortGroup;
    uint maxPartitions = numSortGroups * numPartitionsPerSortGroup;

    if (globalPartition < maxPartitions)
    {
        int a0 = int(sortGroup * numValuesPerSortGroup);
        int a1 = int(min(a0 + chunkSize, numElements));
        int aCount = a1 - a0;
        int b0 = a1;
        int b1 = int(min(b0 + chunkSize, numElements));
        int bCount = b1 - b0;
        // number of values to sort total in this sort group
        int numValues = aCount + bCount;
        // diagonal in the merge matrix we're building
        int diag = int(min(partitionInSortGroup * NumValuesPerThreadGroup, numValues));
        // Find the right merge path for this partition
        int mergePath = MergePath(resources, a0, aCount, b0, bCount, diag, false);

        resources.MergePathPartitions[globalPartition] = mergePath;
    }
}

internal void SerialMerge(
    uniform MergeSortResources resources,
    int a0, int a1, int b0, int b1, int diag, uint numValues, uint out0)
{
    uint i;
    uint aKey;
    uint bKey;
    uint aValue;
    uint bValue;

    aKey = gs_Keys[a0];
    bKey = gs_Keys[b0];
    aValue = gs_Values[a0];
    bValue = gs_Values[b0];

    [unroll]
    for (i = 0u; i < NumValuesPerThread && diag + i < numValues; ++i)
    {
        if (b0 >= b1 || (a0 < a1 && aKey < bKey))
        {
            resources.OutputKeys[out0 + diag + i] = aKey;
            resources.OutputValues[out0 + diag + i] = aValue;
            ++a0;
            aKey = gs_Keys[a0];
            aValue = gs_Values[a0];
        }
        else
        {
            resources.OutputKeys[out0 + diag + i] = bKey;
            resources.OutputValues[out0 + diag + i] = bValue;
            ++b0;
            bKey = gs_Keys[b0];
            bValue = gs_Values[b0];
        }
    }
}

internal void MergeSort(
    in const uint3 workGroupID,
    in const uint localInvocationIndex,
    uniform MergeSortResources resources)
{
    uint i;
    uint key;
    uint value;

    uint chunkSize = resources.Params.ChunkSize;
    uint numElements = resources.Params.NumElements;
    
    uint numChunks = uint(ceil(numElements / float(chunkSize)));
    uint numSortGroups = max(numChunks / 2, 1);
    // find values per group, thread groups per group, and partitions per group
    uint numValuesPerSortGroup = min(chunkSize * 2, numElements);
    uint numThreadGroupsPerSortGroup = uint(ceil(numValuesPerSortGroup / float(NumValuesPerThreadGroup)));
    uint numPartitionsPerSortGroup = numThreadGroupsPerSortGroup + 1;
    // use preceding information to find current sort-group and sub-partition of said group
    uint sortGroup = workGroupID.x / numThreadGroupsPerSortGroup;
    uint partitionVal = workGroupID.x % numThreadGroupsPerSortGroup;
    // use preceding to find index of what global partition to use
    uint globalPartition = (sortGroup * numPartitionsPerSortGroup) + partitionVal;

    // Load required keys about merge paths to take from textures written to previously
    int mergePath0 = resources.MergePathPartitions[globalPartition];
    int mergePath1 = resources.MergePathPartitions[globalPartition + 1];
    int diag0 = int(min(partitionVal * NumValuesPerThreadGroup, numValuesPerSortGroup));
    int diag1 = int(min((partitionVal + 1) * NumValuesPerThreadGroup, numValuesPerSortGroup));

    // Compute chunk ranges in current input set
    int chunkOffsetA0 = int(min(sortGroup * numValuesPerSortGroup, numElements));
    int chunkOffsetA1 = int(min(chunkOffsetA0 + chunkSize, numElements));
    int chunkSizeA = chunkOffsetA1 - chunkOffsetA0;

    int chunkOffsetB0 = chunkOffsetA1;
    int chunkOffsetB1 = int(min(chunkOffsetB0 + chunkSize, numElements));
    int chunkSizeB = chunkOffsetB1 - chunkOffsetB0;

    // total number of values to be sorted
    uint numValues = chunkSizeA + chunkSizeB;

    int a0 = mergePath0;
    int a1 = mergePath1;
    int numA = min(a1 - a0, chunkSizeA);

    int b0 = diag0 - mergePath0;
    int b1 = diag1 - mergePath1;
    int numB = min(b1 - b0, chunkSizeB);

    // diagonal for this particular thread
    int diag = int(localInvocationIndex * NumValuesPerThread);

    // loading of keys and values into local memory
    [unroll]
    for (i = 0; i < NumValuesPerThread; ++i)
    {
        int a = a0 + diag + int(i);
        int b = b0 + (a - a1);

        if (a < a1)
        {
            key = resources.InputKeys[chunkOffsetA0 + a];
            value = resources.InputValues[chunkOffsetA0 + a];
        }
        else
        {
            key = resources.InputKeys[chunkOffsetB0 + b];
            value = resources.InputValues[chunkOffsetB0 + b];
        }

        gs_Keys[diag + i] = key;
        gs_Values[diag + i] = value;
    }

    GroupMemoryBarrierWithGroupSync();

    int mergePath = MergePath(resources, 0, numA, numA, numB, diag, true);

    SerialMerge(resources, mergePath, numA, numA + diag - mergePath, numA + numB, diag0 + diag, numValues, chunkOffsetA0);
}

[shader("compute")] [NumThreads(NumThreadsX, 1, 1)]
void MergeSortCompute(
    uint3 dispatchThreadID: SV_DispatchThreadID,
    uint3 workGroupID: SV_GroupID,
    uint localInvocationIndex: SV_GroupIndex,
    uniform MergeSortResources resources)
{
    switch (SortAlgorithm)
    {
        case 0:
            MergePathPartitionsFn(dispatchThreadID.x, resources);
            break;
        case 1:
            MergeSort(workGroupID, localInvocationIndex, resources);
            break;
    }
}

