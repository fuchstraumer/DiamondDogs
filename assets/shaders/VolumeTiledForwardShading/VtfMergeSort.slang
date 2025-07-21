implementing VolumeTiledForwardShading;
__include VtfTypes;

[SpecializationConstant]
const uint SortAlgorithm = 0;
[SpecializationConstant]
const uint NumThreadsX = 256;
[SpecializationConstant]
const uint NumValuesPerThread = 8;

const uint NumValuesPerThreadGroup = NumThreadsX * NumValuesPerThread;

groupshared uint gs_Keys[NumValuesPerThreadGroup];
groupshared uint gs_Values[NumValuesPerThreadGroup];

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
/// Finds the merge path for two sorted sequences using texture reads.
/// This version loads data directly from a texture/image resource.
/// </summary>
/// <param name="inputKeys">Texture containing the sorted key values</param>
/// <param name="arrayAStart">Starting index of first sorted sequence in the texture</param>
/// <param name="arrayACount">Number of elements in first sequence</param>
/// <param name="arrayBStart">Starting index of second sorted sequence in the texture</param>
/// <param name="arrayBCount">Number of elements in second sequence</param>
/// <param name="diagonal">Target diagonal in the merge matrix</param>
/// <returns>Index where the merge path intersects array A</returns>
int MergePathTexture(uniform Texture2D<uint> inputKeys, int arrayAStart, int arrayACount, int arrayBStart, int arrayBCount, int diagonal)
{
    int searchBegin = max(0, diagonal - arrayBCount);
    int searchEnd = min(diagonal, arrayACount);

    while (searchBegin < searchEnd)
    {
        int searchMid = (searchBegin + searchEnd) >> 1;

        uint keyFromA = inputKeys.Load(int3(arrayAStart + searchMid, 0, 0));
        uint keyFromB = inputKeys.Load(int3(arrayBStart + diagonal - 1 - searchMid, 0, 0));

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

