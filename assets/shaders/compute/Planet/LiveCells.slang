implementing PlanetLab;
__include Buffers;
__include Fibonacci;

static const uint LiveGroupSize = 256u;
static const uint CellsPerThread = 4u;
static const uint CellsPerGroup = LiveGroupSize * CellsPerThread;
// NearestFrontPointCS and UpliftCS run 64 threads to a group on the list
static const uint ListGroupSize = 64u;

groupshared uint GroupLive;
groupshared uint GroupContinental;
groupshared int GroupPosition[3];
groupshared uint GroupCursor;
groupshared uint GroupBase;
groupshared uint GroupSpread;

struct LiveGroup
{
    uint Plate;
    uint FirstCell;
};

LiveGroup FindLiveGroup(uint group)
{
    const uint groupsPerPlate = (Params.PointCount + CellsPerGroup - 1u) / CellsPerGroup;
    LiveGroup result;
    result.Plate = group / groupsPerPlate;
    result.FirstCell = (group % groupsPerPlate) * CellsPerGroup;
    return result;
}

[shader("compute")][numthreads(256,1,1)]
void CountLiveCellsCS(uint3 groupID : SV_GroupID, uint3 laneInGroup : SV_GroupThreadID)
{
    const LiveGroup liveGroup = FindLiveGroup(groupID.x);
    if (laneInGroup.x == 0u)
    {
        GroupLive = 0u;
        GroupContinental = 0u;
        GroupPosition[0] = 0;
        GroupPosition[1] = 0;
        GroupPosition[2] = 0;
    }

    GroupMemoryBarrierWithGroupSync();

    uint live = 0u;
    uint liveMask = 0u;
    [unroll(CellsPerThread)]
    for (uint step = 0u; step < CellsPerThread; ++step)
    {
        const uint cellIndex = liveGroup.FirstCell + step * LiveGroupSize + laneInGroup.x;
        if (cellIndex >= Params.PointCount)
        {
            break;
        }

        const uint flattenedIndex = liveGroup.Plate * Params.PointCount + cellIndex;
        if (LoadAlive(flattenedIndex) != 0u)
        {
            liveMask |= (1u << step);
        }
    }

    uint continental = 0u;
    int3 position = int3(0,0,0);
    // now use live mask to determine which cells are alive without redundant loads
    for (uint step = 0u; step < CellsPerThread; ++step)
    {
        const uint cellIndex = liveGroup.FirstCell + step * LiveGroupSize + laneInGroup.x;
        if (cellIndex >= Params.PointCount)
        {
            break;
        }

        const uint flattenedIndex = liveGroup.Plate * Params.PointCount + cellIndex;
        if ((liveMask & (1u << step)) == 0u)
        {
            continue;
        }

        live += 1u;
        const bool isContinental = LoadContinental(flattenedIndex) != 0u;
        continental += isContinental ? 1u : 0u;
        const float3 latticePoint = LatticePoint(int(cellIndex), int(Params.PointCount));
        position += int3(round(latticePoint * PositionSumScale));
    }

    const uint waveLive = WaveActiveSum(live);
    const uint waveContinental = WaveActiveSum(continental);
    const int3 wavePosition = WaveActiveSum(position);

    if (WaveIsFirstLane())
    {
        InterlockedAdd(GroupLive, waveLive);
        InterlockedAdd(GroupContinental, waveContinental);
        InterlockedAdd(GroupPosition[0], wavePosition.x);
        InterlockedAdd(GroupPosition[1], wavePosition.y);
        InterlockedAdd(GroupPosition[2], wavePosition.z);
    }

    GroupMemoryBarrierWithGroupSync();

    if (laneInGroup.x == 0u && GroupLive > 0u)
    {
        PlateContinentalCount.Add(liveGroup.Plate, GroupContinental);
        const int4 plateCounters = int4(GroupPosition[0], GroupPosition[1], GroupPosition[2], GroupLive);
        AddPlateCounters(liveGroup.Plate * 4u, plateCounters);
    }
}

[shader("compute")][numthreads(32,1,1)]
void OffsetLiveCellsCS(uint3 threadID : SV_DispatchThreadID)
{
    // this runs one dispatch, the dispatch thread is the lane ID
    const uint plate = threadID.x;
    // non-active threads here just won't do any work
    const bool isActive = plate < Params.PlateCount;

    const int4 plateCounters = isActive ? LoadPlateCountersIn(plate * 4u) : 0.xxxx;
    const uint count = isActive ? uint(plateCounters.w) : 0u;
    const uint waveOffset = WavePrefixSum(count);
    const uint total = WaveActiveSum(count);

    if (isActive)
    {
        PlateLiveOffset.Store(plate, waveOffset);
        PlateCapSpread.Store(plate, 0u);
        if (count == 0u)
        {
            PlateCapCenters.Store(plate, float4(0.0f, 0.0f, 0.0f, PlateCapEmpty));
        }
        else
        {
            const float3 positionSum = float3(plateCounters.xyz);
            const float posLength = length(positionSum);
            if (posLength < 1.0e-3f * PositionSumScale * float(count))
            {
                PlateCapCenters.Store(plate, float4(0.0f, 0.0f, 0.0f, PlateCapNone));
            }
            else
            {
                PlateCapCenters.Store(plate, float4(positionSum / posLength.xxx, PlateCapValid));
            }
        }
    }

    if (WaveIsFirstLane())
    {
        PlateLiveOffset.Store(Params.PlateCount, total);
        LiveDispatch.Store(0u, (total + ListGroupSize - 1u) / ListGroupSize);
        LiveDispatch.Store(1u, 1u);
        LiveDispatch.Store(2u, 1u);
    }

}

[shader("compute")][numthreads(256,1,1)]
void ScatterLiveCellsCS(uint3 groupID : SV_GroupID, uint3 laneInGroup : SV_GroupThreadID)
{
    const LiveGroup liveGroup = FindLiveGroup(groupID.x);

    if (laneInGroup.x == 0u)
    {
        GroupCursor = 0u;
        GroupSpread = 0u;
    }

    GroupMemoryBarrierWithGroupSync();

    uint live = 0u;
    uint liveMask = 0u;
    for (uint step = 0u; step < CellsPerThread; ++step)
    {
        const uint cellIndex = liveGroup.FirstCell + step * LiveGroupSize + laneInGroup.x;
        const uint flattenedIndex = liveGroup.Plate * Params.PointCount + cellIndex;
        if (cellIndex < Params.PointCount && LoadAlive(flattenedIndex) != 0u)
        {
            live += 1u;
            liveMask |= (1u << step);
        }
    }

    const uint laneOffset = WavePrefixSum(live);
    const uint waveTotal = WaveActiveSum(live);
    // will set this for the offset *for each wave*, since `laneOffset` is the current
    // threads offset within the wave (relative to 0: waveoffset makes it relative to data)
    uint waveOffset = 0u;
    if (WaveIsFirstLane())
    {
        InterlockedAdd(GroupCursor, waveTotal, waveOffset);
    }

    waveOffset = WaveReadLaneFirst(waveOffset);

    GroupMemoryBarrierWithGroupSync();

    // skip all the atomics/loads/barriers if the group is empty
    if (GroupCursor == 0u)
    {
        return;
    }

    // unfortunately need to barrier again so we can set GroupBase to finish this up
    if (laneInGroup.x == 0u)
    {
        GroupBase = PlateLiveOffset.Load(liveGroup.Plate) + PlateLiveCursor.Add(liveGroup.Plate, GroupCursor);
    }

    // last barrier!
    GroupMemoryBarrierWithGroupSync();

    // get the final index for this thread
    uint resultIdx = GroupBase + waveOffset + laneOffset;
    const float4 plateCap = PlateCapCenters.Load(liveGroup.Plate);
    float spread = 0.0f;

    // changed - use liveMask to test if something is live, don't repeat the load!
    // note: this didn't seem to budge perf, not compared to the early-out we added above, but it's still
    // worth keeping as it avoids redundant loads for cells that are already known to be alive
    for (uint step = 0u; step < CellsPerThread; ++step)
    {
        if ((liveMask & (1u << step)) != 0u)
        {
            const uint cellIndex = liveGroup.FirstCell + step * LiveGroupSize + laneInGroup.x;
            const uint flattenedIndex = liveGroup.Plate * Params.PointCount + cellIndex;
            LiveCell.Store(resultIdx, flattenedIndex);
            resultIdx += 1u;

            const float3 latticePoint = LatticePoint(int(cellIndex), int(Params.PointCount));
            spread = max(spread, 1.0f - dot(latticePoint, plateCap.xyz));
        }
    }

    const uint spreadUint = asuint(spread);
    const uint waveSpread = WaveActiveMax(spreadUint);
    if (WaveIsFirstLane())
    {
        InterlockedMax(GroupSpread, waveSpread);
    }

    GroupMemoryBarrierWithGroupSync();

    if (laneInGroup.x == 0u && plateCap.w == PlateCapValid)
    {
        PlateCapSpread.Max(liveGroup.Plate, GroupSpread);
    }

}