implementing PlanetLab;
__include Buffers;


static const uint SearchUnrollDepth = 4u;

struct NearestCandidate
{
    float Cosine;
    int Index;
};

NearestCandidate NoCandidate()
{
    return { -2.0f, -1 };
}

NearestCandidate MakeCandidate(float3 worldPoint, float4 frontPoint)
{
    return { dot(worldPoint, frontPoint.xyz), int(asuint(frontPoint.w)) };
}

NearestCandidate Nearest(NearestCandidate lhs, NearestCandidate rhs)
{
    return (rhs.Cosine > lhs.Cosine) ? rhs : lhs;
}

void StoreFrontPointData(uint where, float3 position, uint point)
{
    FrontPoints.Store(where, float4(position, asfloat(point)));
}

/*
    The job of these entry points is to process all the points on subduction fronts,
    where one plate is diving underneath another. These are the points from previous 
    stages where we set FrontSubduction to 1. At N=100k, there are about 1200 of these
    (so there are relatively few of them compared to the total number of points, which
    is to be expected in a plate tectonics simulation if you think about it - most points
    are just on plates floating on the mantle!).
*/

[shader("compute")]
[numthreads(64, 1, 1)]
void CountFrontPointsCS(uint3 threadId : SV_DispatchThreadID)
{
    const uint point = threadId.x;
    if (point >= Params.PointCount)
    {
        return;
    }

    const FrontState frontState = LoadFrontStateIn(point);
    if (frontState.GetSubduction() != 1u)
    {
        return;
    }

    const OwnersData owners = LoadOwnersIn(point);
    int plate = owners.GetBestPlate();
    PlateFrontPointCount.Add(uint(plate), 1u);
}

[shader("compute")]
[numthreads(32, 1, 1)]
void OffsetFrontPointsCS(uint3 threadId : SV_DispatchThreadID)
{
    const uint plate = threadId.x;
    const bool isActive = plate < Params.PlateCount;
    const uint count = isActive ? PlateFrontPointCountIn.Load(plate) : 0u;
    const uint waveOffset = WavePrefixSum(count);
    const uint waveTotal = WaveActiveSum(count);

    if (isActive)
    {
        PlateFrontPointOffset.Store(plate, waveOffset);
    }

    if (WaveIsFirstLane())
    {
        PlateFrontPointOffset.Store(Params.PlateCount, waveTotal);
        FrontTileDispatch.Store(0u, (waveTotal + 63u) / 64u);
        FrontTileDispatch.Store(1u, 1u);
        FrontTileDispatch.Store(2u, 1u);
    }
}

[shader("compute")]
[numthreads(64, 1, 1)]
void ScatterFrontPointsCS(uint3 threadId : SV_DispatchThreadID)
{
    const uint point = threadId.x;
    // skip points out of range or not on a subduction front
    if (point >= Params.PointCount)
    {
        return;
    }

    const FrontState frontState = LoadFrontStateIn(point);
    if (frontState.GetSubduction() != 1u)
    {
        return;
    }

    // determine which slot this current point goes into - then convert it from the flattened
    // integral slot in the lattice to an actual 3D point in space: write that out, along
    // with it's point index into a dense array of front points for further processing.
    const OwnersData owners = LoadOwnersIn(point);
    const uint plate = uint(owners.GetBestPlate());
    // `PlateFrontPointCursor` is an atomic counter, so multiple threads can increment it concurrently
    // to write to the same plate's front point slots without causing race conditions.
    const uint slot = PlateFrontPointOffset.Load(plate) + PlateFrontPointCursor.Add(plate, 1u);
    const float3 position = HealpixPixToVec(uint(point), Params.HealpixOrder);
    StoreFrontPointData(slot, position, point);
}

static const uint SearchBlockSize = 64u;
static const uint MaxWavesPerGroup = 4u;
static const float MaxFrontAngleRad = 1850.0f / 6370.0f;
groupshared float4 SharedFrontPoints[SearchBlockSize];
groupshared float3 SharedGroupCenterSum[MaxWavesPerGroup];
groupshared float SharedGroupMinCos[MaxWavesPerGroup];
groupshared float3 SharedGroupCenter;
groupshared float SharedGroupAngle;
groupshared float3 SharedTileCenterSum[MaxWavesPerGroup];
groupshared float SharedTileMinCos[MaxWavesPerGroup];
groupshared float3 SharedTileCenter;
groupshared float SharedTileAngle;
groupshared uint SharedGroupHasContinentWaves[MaxWavesPerGroup];
groupshared uint SharedGroupHasContinent;

[shader("compute")]
[numthreads(64, 1, 1)]
void BuildFrontTileCapsCS(uint3 groupID : SV_GroupID, uint3 laneInGroup : SV_GroupThreadID)
{
    const uint totalFronts = PlateFrontPointOffset.Load(Params.PlateCount);
    const uint slot = groupID.x * SearchBlockSize + laneInGroup.x;
    const bool valid = slot < totalFronts;
    const float3 pt = valid ? FrontPoints.Load(slot).xyz : float3(0.0f, 0.0f, 0.0f);

    const uint waveSize = WaveGetLaneCount();
    const uint waveCount = (SearchBlockSize + waveSize - 1u) / waveSize;
    const uint waveIndex = laneInGroup.x / waveSize;

    const float3 waveSum = WaveActiveSum(pt);
    if (WaveIsFirstLane())
    {
        SharedTileCenterSum[waveIndex] = waveSum;
    }

    GroupMemoryBarrierWithGroupSync();

    if (laneInGroup.x == 0u)
    {
        float3 sum = float3(0.0f, 0.0f, 0.0f);
        for (uint w = 0u; w < waveCount; ++w)
        {
            sum += SharedTileCenterSum[w];
        }
        const float len = length(sum);
        if (len < 1e-5f)
        {
            SharedTileCenter = float3(0.0f, 0.0f, 1.0f);
            SharedTileAngle = 3.14159265f;
        }
        else
        {
            SharedTileCenter = sum / len;
            SharedTileAngle = 0.0f;
        }
    }

    GroupMemoryBarrierWithGroupSync();

    const float3 tileCenter = SharedTileCenter;
    const float tileCos = valid ? dot(pt, tileCenter) : 1.0f;
    const float waveMinCos = WaveActiveMin(tileCos);
    if (WaveIsFirstLane())
    {
        SharedTileMinCos[waveIndex] = waveMinCos;
    }

    GroupMemoryBarrierWithGroupSync();

    if (laneInGroup.x == 0u)
    {
        float tileAngle = SharedTileAngle;
        if (tileAngle == 0.0f)
        {
            float minCos = 1.0f;
            for (uint w = 0u; w < waveCount; ++w)
            {
                minCos = min(minCos, SharedTileMinCos[w]);
            }
            tileAngle = acos(clamp(minCos, -1.0f, 1.0f));
        }
        FrontTileCaps.Store(groupID.x, float4(tileCenter, tileAngle));
    }
}

[shader("compute")]
[numthreads(64, 1, 1)]
void FrontDistanceAndUpliftCS(uint3 threadId : SV_DispatchThreadID,
                              uint3 groupID : SV_GroupID,
                              uint3 laneInGroup : SV_GroupThreadID)
{
    const uint idxLimit = PlateLiveOffset.Load(Params.PlateCount);
    const bool isLive = threadId.x < idxLimit;

    const uint groupFirstCell = groupID.x * SearchBlockSize;
    const uint groupLastCell = min(groupFirstCell + SearchBlockSize, idxLimit) - 1u;
    const uint firstPlate = LiveCell.Load(groupFirstCell) / Params.PointCount;
    const uint sharedBegin = PlateFrontPointOffset.Load(firstPlate);
    const uint lastPlate = (LiveCell.Load(groupLastCell) / Params.PointCount) + 1u;
    const uint sharedEnd = PlateFrontPointOffset.Load(lastPlate);

    uint flattenedPoint = 0u;
    float3 worldPoint = float3(0.0f, 0.0f, 0.0f);
    uint begin = 0u;
    uint end = 0u;
    CrustFlagBits crustFlags = { 0u };
    bool isContinental = false;

    if (isLive)
    {
        flattenedPoint = LiveCell.Load(threadId.x);
        crustFlags = LoadCrustFlags(flattenedPoint);
        isContinental = crustFlags.IsContinental();
        const uint plateIdx = flattenedPoint / Params.PointCount;
        const uint plateCell = flattenedPoint % Params.PointCount;
        const float3x4 plateTransform = LoadRotation(plateIdx);
        const float3 latticePoint = HealpixPixToVec(uint(plateCell), Params.HealpixOrder);
        worldPoint = PlateToWorld(latticePoint, plateTransform);
        begin = PlateFrontPointOffset.Load(plateIdx);
        end = PlateFrontPointOffset.Load(plateIdx + 1u);
    }

    const uint waveSize = WaveGetLaneCount();
    const uint waveCount = (SearchBlockSize + waveSize - 1u) / waveSize;
    const uint waveIndex = laneInGroup.x / waveSize;

    const float3 waveGroupSum = WaveActiveSum(worldPoint);
    const bool waveAnyContinent = WaveActiveAnyTrue(isContinental);
    if (WaveIsFirstLane())
    {
        SharedGroupCenterSum[waveIndex] = waveGroupSum;
        SharedGroupHasContinentWaves[waveIndex] = waveAnyContinent ? 1u : 0u;
    }

    GroupMemoryBarrierWithGroupSync();

    if (laneInGroup.x == 0u)
    {
        float3 sum = float3(0.0f, 0.0f, 0.0f);
        uint anyCont = 0u;
        for (uint w = 0u; w < waveCount; ++w)
        {
            sum += SharedGroupCenterSum[w];
            anyCont |= SharedGroupHasContinentWaves[w];
        }
        SharedGroupHasContinent = anyCont;
        const float len = length(sum);
        if (len < 1e-5f)
        {
            SharedGroupCenter = float3(0.0f, 0.0f, 1.0f);
            SharedGroupAngle = 3.14159265f;
        }
        else
        {
            SharedGroupCenter = sum / len;
            SharedGroupAngle = 0.0f;
        }
    }

    GroupMemoryBarrierWithGroupSync();

    const float3 groupCenter = SharedGroupCenter;
    const bool groupHasContinent = SharedGroupHasContinent != 0u;
    const float groupCos = isLive ? dot(worldPoint, groupCenter) : 1.0f;
    const float waveGroupMinCos = WaveActiveMin(groupCos);
    if (WaveIsFirstLane())
    {
        SharedGroupMinCos[waveIndex] = waveGroupMinCos;
    }

    GroupMemoryBarrierWithGroupSync();

    if (laneInGroup.x == 0u && SharedGroupAngle == 0.0f)
    {
        float minCos = 1.0f;
        for (uint w = 0u; w < waveCount; ++w)
        {
            minCos = min(minCos, SharedGroupMinCos[w]);
        }
        SharedGroupAngle = acos(clamp(minCos, -1.0f, 1.0f));
    }

    GroupMemoryBarrierWithGroupSync();

    const float groupAngle = SharedGroupAngle;
    const float groupReachRad = groupHasContinent ? (1850.0f / 6370.0f) : (500.0f / 6370.0f);

    NearestCandidate nearest0 = NoCandidate();
    NearestCandidate nearest1 = NoCandidate();
    NearestCandidate nearest2 = NoCandidate();
    NearestCandidate nearest3 = NoCandidate();

    if (sharedBegin < sharedEnd)
    {
        const uint alignedSharedBegin = sharedBegin & ~63u;
        for (uint sharedStart = alignedSharedBegin; sharedStart < sharedEnd; sharedStart += SearchBlockSize)
        {
            const float4 tileCap = FrontTileCaps.Load(sharedStart >> 6u);
            const float maxAllowed = min(3.14159265f, groupReachRad + groupAngle + tileCap.w);
            if (clamp(dot(groupCenter, tileCap.xyz), -1.0f, 1.0f) < cos(maxAllowed))
            {
                continue;
            }

            const uint loadSlot = sharedStart + laneInGroup.x;
            if (loadSlot < sharedEnd)
            {
                SharedFrontPoints[laneInGroup.x] = FrontPoints.Load(loadSlot);
            }

            GroupMemoryBarrierWithGroupSync();

            const uint first = max(begin, sharedStart);
            const uint last = min(end, min(sharedStart + SearchBlockSize, sharedEnd));

            uint slot = first;
            for (; slot + SearchUnrollDepth <= last; slot += SearchUnrollDepth)
            {
                const uint sharedSlot = slot - sharedStart;
                nearest0 = Nearest(nearest0, MakeCandidate(worldPoint, SharedFrontPoints[sharedSlot]));
                nearest1 = Nearest(nearest1, MakeCandidate(worldPoint, SharedFrontPoints[sharedSlot + 1u]));
                nearest2 = Nearest(nearest2, MakeCandidate(worldPoint, SharedFrontPoints[sharedSlot + 2u]));
                nearest3 = Nearest(nearest3, MakeCandidate(worldPoint, SharedFrontPoints[sharedSlot + 3u]));
            }

            for (; slot < last; ++slot)
            {
                nearest0 = Nearest(nearest0, MakeCandidate(worldPoint, SharedFrontPoints[slot - sharedStart]));
            }

            GroupMemoryBarrierWithGroupSync();
        }
    }

    if (!isLive)
    {
        return;
    }

    const NearestCandidate best = Nearest(Nearest(nearest0, nearest1), Nearest(nearest2, nearest3));
    float distance = asfloat(0x7f800000);
    if (best.Index >= 0)
    {
        const float3 delta = worldPoint - HealpixPixToVec(uint(best.Index), Params.HealpixOrder);
        distance = 6370.0f * 2.0f * asin(clamp(0.5f * length(delta), 0.0f, 1.0f));
    }

    StoreCellState(flattenedPoint, best.Index, distance);

    if (best.Index < 0 && !isContinental)
    {
        return;
    }

    CrustStateData crustState = LoadCrustState(flattenedPoint);
    float relief = crustState.GetRelief();

    if (best.Index >= 0)
    {
        const float reach = isContinental ? ContinentalReachKm : IslandArcReachKm;
        const float shape = SubductionProfile(distance, reach);
        const FrontState frontState = LoadFrontStateIn(uint(best.Index));
        const float rate = BaseUplift * shape * frontState.GetStrength();
        const float dt = Params.TimeStepMy;
        relief += rate * dt;

        if (rate > ActiveUplift)
        {
            const int updatedOrogeny = isContinental ? OrogenyAndean : OrogenyIslandArc;
            crustState.SetOrogenyType(updatedOrogeny);
            crustState.SetOrogenyTime(UpliftConstants.Time);
        }
    }

    if (isContinental)
    {
        relief *= 1.0f - ContinentErosion * Params.TimeStepMy / PeakElevation;
    }

    relief = min(relief, PeakElevation);

    if (!isContinental && (relief > AccretionRelief))
    {
        const uint flagBits = PackCrustFlags(crustFlags.IsAlive(), true);
        StoreCrustFlags(flattenedPoint, flagBits);
        relief = AccretionStartRelief;
    }

    crustState.SetRelief(relief);
    StoreCrustState(flattenedPoint, crustState);
}

[shader("compute")]
[numthreads(64, 1, 1)]
void NearestFrontPointCS(uint3 threadId : SV_DispatchThreadID,
                         uint groupID : SV_GroupID,
                         uint laneInGroup : SV_GroupThreadID)
{
    // The brute force search to find nearest points to the subducting points: our initial
    // thought was to use jump flooding for this, but given our point count (fairly small),
    // a simple brute force approach is likely more than sufficient for our needs.

    // every lane now takes part in the groupshared loads and barriers - so a lane past
    // the end of the list is aware of this, but doesn't return since it has to help still
    const uint idxLimit = PlateLiveOffset.Load(Params.PlateCount);
    const bool isLive = threadId.x < idxLimit;

    // the live list is in plate order, and so are the front points: the plates of a groups
    // first and last cells bound one contiguous range of slots, even if the group spans multiple plates
    const uint groupFirstCell = groupID.x * SearchBlockSize;
    const uint groupLastCell = min(groupFirstCell + SearchBlockSize, idxLimit) - 1u;
    const uint firstPlate = LiveCell.Load(groupFirstCell) / Params.PointCount;
    const uint sharedBegin = PlateFrontPointOffset.Load(firstPlate);
    const uint lastPlate = (LiveCell.Load(groupLastCell) / Params.PointCount) + 1u;
    const uint sharedEnd = PlateFrontPointOffset.Load(lastPlate);

    uint flattenedPoint = 0u;
    float3 worldPoint = float3(0.0f, 0.0f, 0.0f);
    uint begin = 0u;
    uint end = 0u;

    if (isLive)
    {
        flattenedPoint = LiveCell.Load(threadId.x);
        const uint plateIdx = flattenedPoint / Params.PointCount;
        const uint plateCell = flattenedPoint % Params.PointCount;
        const float3x4 plateTransform = LoadRotation(plateIdx);
        const float3 latticePoint = HealpixPixToVec(uint(plateCell), Params.HealpixOrder);
        worldPoint = PlateToWorld(latticePoint, plateTransform);
        begin = PlateFrontPointOffset.Load(plateIdx);
        end = PlateFrontPointOffset.Load(plateIdx + 1u);
    }

    const uint waveSize = WaveGetLaneCount();
    const uint waveCount = (SearchBlockSize + waveSize - 1u) / waveSize;
    const uint waveIndex = laneInGroup.x / waveSize;

    const float3 waveGroupSum = WaveActiveSum(worldPoint);
    if (WaveIsFirstLane())
    {
        SharedGroupCenterSum[waveIndex] = waveGroupSum;
    }

    GroupMemoryBarrierWithGroupSync();

    if (laneInGroup.x == 0u)
    {
        float3 sum = float3(0.0f, 0.0f, 0.0f);
        for (uint w = 0u; w < waveCount; ++w)
        {
            sum += SharedGroupCenterSum[w];
        }
        const float len = length(sum);
        if (len < 1e-5f)
        {
            SharedGroupCenter = float3(0.0f, 0.0f, 1.0f);
            SharedGroupAngle = 3.14159265f;
        }
        else
        {
            SharedGroupCenter = sum / len;
            SharedGroupAngle = 0.0f;
        }
    }

    GroupMemoryBarrierWithGroupSync();

    const float3 groupCenter = SharedGroupCenter;
    const float groupCos = isLive ? dot(worldPoint, groupCenter) : 1.0f;
    const float waveGroupMinCos = WaveActiveMin(groupCos);
    if (WaveIsFirstLane())
    {
        SharedGroupMinCos[waveIndex] = waveGroupMinCos;
    }

    GroupMemoryBarrierWithGroupSync();

    if (laneInGroup.x == 0u && SharedGroupAngle == 0.0f)
    {
        float minCos = 1.0f;
        for (uint w = 0u; w < waveCount; ++w)
        {
            minCos = min(minCos, SharedGroupMinCos[w]);
        }
        SharedGroupAngle = acos(clamp(minCos, -1.0f, 1.0f));
    }
    GroupMemoryBarrierWithGroupSync();

    const float groupAngle = SharedGroupAngle;

    NearestCandidate nearest0 = NoCandidate();
    NearestCandidate nearest1 = NoCandidate();
    NearestCandidate nearest2 = NoCandidate();
    NearestCandidate nearest3 = NoCandidate();

    for (uint sharedStart = sharedBegin; sharedStart < sharedEnd; sharedStart += SearchBlockSize)
    {
        const uint loadSlot = sharedStart + laneInGroup.x;
        const bool validTileSlot = loadSlot < sharedEnd;
        float3 tilePoint = float3(0.0f, 0.0f, 0.0f);
        if (validTileSlot)
        {
            const float4 loaded = FrontPoints.Load(loadSlot);
            SharedFrontPoints[laneInGroup.x] = loaded;
            tilePoint = loaded.xyz;
        }

        const float3 waveTileSum = WaveActiveSum(tilePoint);
        if (WaveIsFirstLane())
        {
            SharedTileCenterSum[waveIndex] = waveTileSum;
        }

        GroupMemoryBarrierWithGroupSync();

        if (laneInGroup.x == 0u)
        {
            float3 sum = float3(0.0f, 0.0f, 0.0f);
            for (uint w = 0u; w < waveCount; ++w)
            {
                sum += SharedTileCenterSum[w];
            }
            const float len = length(sum);
            if (len < 1e-5f)
            {
                SharedTileCenter = float3(0.0f, 0.0f, 1.0f);
                SharedTileAngle = 3.14159265f;
            }
            else
            {
                SharedTileCenter = sum / len;
                SharedTileAngle = 0.0f;
            }
        }

        GroupMemoryBarrierWithGroupSync();

        const float3 tileCenter = SharedTileCenter;
        const float tileCos = validTileSlot ? dot(tilePoint, tileCenter) : 1.0f;
        const float waveTileMinCos = WaveActiveMin(tileCos);
        if (WaveIsFirstLane())
        {
            SharedTileMinCos[waveIndex] = waveTileMinCos;
        }

        GroupMemoryBarrierWithGroupSync();

        if (laneInGroup.x == 0u && SharedTileAngle == 0.0f)
        {
            float minCos = 1.0f;
            for (uint w = 0u; w < waveCount; ++w)
            {
                minCos = min(minCos, SharedTileMinCos[w]);
            }
            SharedTileAngle = acos(clamp(minCos, -1.0f, 1.0f));
        }
        
        GroupMemoryBarrierWithGroupSync();

        const float tileAngle = SharedTileAngle;
        const float maxAllowedAngle = min(3.14159265f, MaxFrontAngleRad + groupAngle + tileAngle);
        if (clamp(dot(groupCenter, tileCenter), -1.0f, 1.0f) >= cos(maxAllowedAngle))
        {
            // check this lanes own plate range, inside this shared block!
            const uint first = max(begin, sharedStart);
            const uint last = min(end, min(sharedStart + SearchBlockSize, sharedEnd));

            uint slot = first;
            for(; slot + SearchUnrollDepth <= last; slot += SearchUnrollDepth)
            {
                const uint sharedSlot = slot - sharedStart;
                nearest0 = Nearest(nearest0, MakeCandidate(worldPoint, SharedFrontPoints[sharedSlot]));
                nearest1 = Nearest(nearest1, MakeCandidate(worldPoint, SharedFrontPoints[sharedSlot + 1]));
                nearest2 = Nearest(nearest2, MakeCandidate(worldPoint, SharedFrontPoints[sharedSlot + 2]));
                nearest3 = Nearest(nearest3, MakeCandidate(worldPoint, SharedFrontPoints[sharedSlot + 3]));
            }

            for (; slot < last; ++slot)
            {
                nearest0 = Nearest(nearest0, MakeCandidate(worldPoint, SharedFrontPoints[slot - sharedStart]));
            }
        }

        GroupMemoryBarrierWithGroupSync();
    }

    // *now* "dead" cell threads can exit, as they have no further work to help with
    if (!isLive)
    {
        return;
    }

    const NearestCandidate best = Nearest(Nearest(nearest0, nearest1), Nearest(nearest2, nearest3));
    float distance = asfloat(0x7f800000);
    if (best.Index >= 0)
    {
        const float3 delta = worldPoint - HealpixPixToVec(uint(best.Index), Params.HealpixOrder);
        distance = 6370.0f * 2.0f * asin(clamp(0.5f * length(delta), 0.0f, 1.0f));
    }

    StoreCellState(flattenedPoint, best.Index, distance);
}
