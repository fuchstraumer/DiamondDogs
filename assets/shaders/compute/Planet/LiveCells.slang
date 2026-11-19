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
    uint continental = 0u;
    int3 position = int3(0,0,0);

    for (uint step = 0u; step < CellsPerThread; ++step)
    {
        const uint cellIndex = liveGroup.FirstCell + step * LiveGroupSize + laneInGroup.x;
        if (cellIndex >= Params.PointCount)
        {
            break;
        }

        const uint flattenedIndex = liveGroup.Plate * Params.PointCount + cellIndex;
        if (LoadAlive(flattenedIndex) == 0u)
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
        PlateCellCount.Add(liveGroup.Plate, GroupLive);
        PlateContinentalCount.Add(liveGroup.Plate, GroupContinental);
        const int3 positionSum = int3(GroupPosition[0], GroupPosition[1], GroupPosition[2]);
        AddPlatePositionSum(liveGroup.Plate * 3u, positionSum);
    }
}

[shader("compute")][numthreads(1,1,1)]
void OffsetLiveCellsCS(uint3 threadID : SV_DispatchThreadID)
{
    // run an offset pass to fill the buffers the next stage will need - and write
    // the dispatch size for the next pass
    uint sum = 0u;
    for (uint plate = 0u; plate < Params.PlateCount; ++plate)
    {
        const uint count = PlateCellCount.Load(plate);
        PlateLiveOffset.Store(plate, sum);
        sum += count;

        // now move to compute cap for the next step: ScatterLiveCellsCS will handle
        // the spread parameter
        PlateCapSpread.Store(plate, 0u);
        if (count == 0u)
        {
            // we use the .w component as a status flag
            PlateCapCenters.Store(plate, float4(0.0f, 0.0f, 0.0f, PlateCapEmpty));
            continue;
        }

        // live cells spread evenly over the sphere have no useful center, so the 
        // plate won't get a cap
        const float3 positionSum = float3(LoadPlatePositionSum(plate * 3u));
        const float posLength = length(positionSum);
        if (posLength < 1.0e-3f * PositionSumScale * float(count))
        {
            PlateCapCenters.Store(plate, float4(0.0f, 0.0f, 0.0f, PlateCapNone));
            continue;
        }

        PlateCapCenters.Store(plate, float4(positionSum / posLength.xxx, PlateCapValid));
    }

    PlateLiveOffset.Store(Params.PlateCount, sum);

    LiveDispatch.Store(0u, (sum + ListGroupSize - 1u) / ListGroupSize);
    LiveDispatch.Store(1u, 1u);
    LiveDispatch.Store(2u, 1u);
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
    for (uint step = 0u; step < CellsPerThread; ++step)
    {
        const uint cellIndex = liveGroup.FirstCell + step * LiveGroupSize + laneInGroup.x;
        const uint flattenedIndex = liveGroup.Plate * Params.PointCount + cellIndex;
        if (cellIndex < Params.PointCount && LoadAlive(flattenedIndex) != 0u)
        {
            live += 1u;
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

    for (uint step = 0u; step < CellsPerThread; ++step)
    {
        const uint cellIndex = liveGroup.FirstCell + step * LiveGroupSize + laneInGroup.x;
        const uint flattenedIndex = liveGroup.Plate * Params.PointCount + cellIndex;
        if (cellIndex < Params.PointCount && LoadAlive(flattenedIndex) != 0u)
        {
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