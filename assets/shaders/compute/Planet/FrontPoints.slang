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
    const uint frontType = frontState.GetSubduction();
    if (frontType == FrontTypeNone)
    {
        return;
    }

    const OwnersData owners = LoadOwnersIn(point);
    const int plate = owners.GetBestPlate();
    PlateFrontPointCount.Add(uint(plate), 1u);
    if (frontType == FrontTypeCollision && owners.GetSecondPlate() >= 0)
    {
        PlateFrontPointCount.Add(uint(owners.GetSecondPlate()), 1u);
    }
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
    const uint frontType = frontState.GetSubduction();
    if (frontType == FrontTypeNone)
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

    if (frontType == FrontTypeCollision && owners.GetSecondPlate() >= 0)
    {
        const uint secondPlate = uint(owners.GetSecondPlate());
        const uint secondSlot = PlateFrontPointOffset.Load(secondPlate) + PlateFrontPointCursor.Add(secondPlate, 1u);
        StoreFrontPointData(secondSlot, position, point);
    }
}

static const uint SortGroupSize = 256u;
static const uint MaxSortableFrontsPerPlate = 2048u;
groupshared uint SharedSortPixels[MaxSortableFrontsPerPlate];

[shader("compute")][numthreads(SortGroupSize, 1, 1)]
void SortFrontPointsCS(uint3 groupID : SV_GroupID, uint3 laneInGroup : SV_GroupThreadID)
{
    const uint plate = groupID.x;
    if (plate >= Params.PlateCount)
    {
        return;
    }

    const uint begin = PlateFrontPointOffset.Load(plate);
    const uint end = PlateFrontPointOffset.Load(plate + 1u);
    const uint count = end - begin;

    if (count <= 64u || count > MaxSortableFrontsPerPlate)
    {
        return;
    }

    // compute the next power-of-2 greater than or equal to count, with a minimum of 128
    const uint paddedCount = max(128u, 1u << (firstbithigh(count - 1u) + 1u));

    // Setup register array: we do 8 keys locally, purely on this thread, before bouncing out to waves then SMEM
    static const uint KeysPerThread = 8u; // making a constant for now so we can tweak and tune if needed (occupancy)
    uint localKeys[KeysPerThread];
    const uint waveSize = WaveGetLaneCount();
    const uint waveBlockSize = waveSize * KeysPerThread;
    const uint thisLaneLocal = WaveGetLaneIndex();
    const uint thisLaneGlobal = laneInGroup.x;
    // localKeyStart needs to be relative to all waves, while `thisLaneLocal` is for our wave-relative indexing. oops!
    const uint localKeyStart = thisLaneGlobal * KeysPerThread;

    // try something different; perfectly coalesced load into SMEM, and then one barrier
    // the previous approach was hitting different cache lines for each thread's keys, causing inefficient memory access patterns
    // this should be cleaner, but it requires one barrier: benchmark to compare.
    for (uint i = thisLaneGlobal; i < paddedCount; i += SortGroupSize)
    {
        SharedSortPixels[i] = (i < count) ? asuint(FrontPoints.Load(begin + i).w) : 0xFFFFFFFFu;
    }

    GroupMemoryBarrierWithGroupSync(); // ensure all threads have finished writing to SharedSortPixels before proceeding

    // populate the threadlocal array. this is going to hammer DRAM as each thread is going to get 8 
    // values from the bound resource, but we only have to do this once and we can avoid barriers
    // (also the scheduler can hide memory latency by interleaving other threads' work *BECAUSE* no barriers)
    [unroll]
    for (uint k = 0u; k < KeysPerThread; ++k)
    {
        const uint globalIdx = localKeyStart + k;
        localKeys[k] = (globalIdx < paddedCount) ? SharedSortPixels[globalIdx] : 0xFFFFFFFFu;
    }

    // now run the local sort
    [unroll]
    for (uint stepSize = 2u; stepSize <= KeysPerThread; stepSize <<= 1u) // <<= 1u instead of multiplying by 2 each iteration
    {
        [unroll]
        for (uint stride = stepSize >> 1u; stride > 0u; stride >>= 1u) // again, shift instead of doing math bc this is power-of-2
        {
            [unroll]
            for (uint i = 0; i < KeysPerThread; ++i)
            {
                const bool isLow = (i & stride) == 0u;
                if (isLow)
                {
                    const uint partnerIdx = i | stride;
                    const bool ascending = ((localKeyStart + i) & stepSize) == 0u;
                    const uint key = localKeys[i];
                    const uint partnerKey = localKeys[partnerIdx];
                    const uint minKey = min(key, partnerKey);
                    const uint maxKey = max(key, partnerKey);
                    localKeys[i] = ascending ? minKey : maxKey;
                    localKeys[partnerIdx] = ascending ? maxKey : minKey;
                }
            }
        }
    }

    // thread-local step done: now we go out to the waves
    const uint maxWaveStepSize = min(waveBlockSize, paddedCount);
    const uint waveStartingStep = KeysPerThread << 1u; // 2 times the keys per thread
    for (uint waveStepSize = waveStartingStep; waveStepSize <= maxWaveStepSize; waveStepSize <<= 1u)
    {
        for (uint waveStride = waveStepSize >> 1u; waveStride > 0u; waveStride >>= 1u)
        {
            if (waveStride >= KeysPerThread)
            {
                const uint laneStride = waveStride / KeysPerThread;
                const uint partnerLane = thisLaneLocal ^ laneStride; // xor to find partner index
                const bool isLow = (thisLaneLocal & laneStride) == 0u; // similar approach as fft butterfly...
                [unroll]
                for (uint i = 0u; i < KeysPerThread; ++i)
                {
                    const bool ascending = ((localKeyStart + i) & waveStepSize) == 0u;
                    const uint laneValue = localKeys[i];
                    // should be a super fast lane-to-lane read within the wave (check nsight)
                    const uint partnerLaneValue = WaveReadLaneAt(laneValue, partnerLane);
                    localKeys[i] = (isLow == ascending) ? min(laneValue, partnerLaneValue) : max(laneValue, partnerLaneValue);
                }
            }
            else
            {
                [unroll]
                for (uint i = 0u; i < KeysPerThread; ++i)
                {
                    const bool isLow = (i & waveStride) == 0u;
                    if (isLow)
                    {
                        const uint partnerIdx = i | waveStride;
                        const bool ascending = ((localKeyStart + i) & waveStepSize) == 0u;
                        const uint key = localKeys[i];
                        const uint partnerKey = localKeys[partnerIdx];
                        const uint minKey = min(key, partnerKey);
                        const uint maxKey = max(key, partnerKey);
                        localKeys[i] = ascending ? minKey : maxKey;
                        localKeys[partnerIdx] = ascending ? maxKey : minKey;
                    }
                }
            }
        }
    }

    // now flush the sorted thread and wave blocks back to SMEM
    // any localkey underneath padded count is now one of the writers
    if (localKeyStart < paddedCount)
    {
        [unroll]
        for (uint i = 0u; i < KeysPerThread; ++i)
        {
            SharedSortPixels[localKeyStart + i] = localKeys[i];
        }
    }

    // first barrier, way later than our initial implementation... we got a titanic amount of work done,
    // and having the barrier this late means the scheduler has ample opportunity to hide memory latency
    // by interleaving other threads' work before we synchronize.
    GroupMemoryBarrierWithGroupSync();

    // phase 3: the cross-warp shared memory sort. the slowest part because of all the barriers, but now
    // we've reduced the workload into larger chunks (so each wave now handles more data, keeping it busier
    // between barriers, and reducing the barriers as much as possible compared to a naive implementation)
    for (uint crossWaveStepSize = waveBlockSize << 1u; crossWaveStepSize <= paddedCount; crossWaveStepSize <<= 1u)
    {
        for (uint crossWaveStrideSize = crossWaveStepSize >> 1u; crossWaveStrideSize > 0u; crossWaveStrideSize >>= 1u)
        {
            if (crossWaveStrideSize >= waveBlockSize)
            {
                // have to use SMEM for this part
                for (uint pairIdx = thisLaneGlobal; pairIdx < (paddedCount >> 1u); pairIdx += SortGroupSize)
                {
                    const uint low = ((pairIdx & ~(crossWaveStrideSize - 1u)) << 1u) | (pairIdx & (crossWaveStrideSize - 1u));
                    const uint high = low | crossWaveStrideSize;
                    const bool ascending = ((low & crossWaveStepSize) == 0u);
                    const uint lowKey = SharedSortPixels[low];
                    const uint highKey = SharedSortPixels[high];
                    if ((ascending && (lowKey > highKey)) || (!ascending && (lowKey < highKey)))
                    {
                        SharedSortPixels[low] = highKey;
                        SharedSortPixels[high] = lowKey;
                    }
                }
                GroupMemoryBarrierWithGroupSync(); // sadge
            }
            else
            {
                // once stride is below wave capacity, we can get back to wave intrinsics to crush what's left
                if (localKeyStart < paddedCount)
                {
                    // fill localKeys again: same deal as above, the deferred barrier means warp scheduler
                    // can probably overlap memory fetches with computation in other warps, improving throughput
                    [unroll]
                    for (uint k = 0; k < KeysPerThread; ++k)
                    {
                        localKeys[k] = SharedSortPixels[localKeyStart + k];
                    }
                }

                // divide inner stride by 2 each iteration
                for (uint innerStride = crossWaveStrideSize; innerStride > 0u; innerStride >>= 1u)
                {
                    if (innerStride >= KeysPerThread)
                    {
                        const uint laneStride = innerStride / KeysPerThread;
                        const uint partnerLane = thisLaneLocal ^ laneStride;
                        const bool isLow = (thisLaneLocal & laneStride) == 0u;
                        [unroll]
                        for (uint i = 0; i < KeysPerThread; ++i)
                        {
                            const bool ascending = ((localKeyStart + i) & crossWaveStepSize) == 0u;
                            const uint laneKey = localKeys[i];
                            const uint partnerKey = WaveReadLaneAt(laneKey, partnerLane);
                            localKeys[i] = (isLow == ascending) ? min(laneKey, partnerKey) : max(laneKey, partnerKey);
                        }
                    }
                    else
                    {
                        // entirely thread-local comparison and swapping
                        [unroll]
                        for (uint i = 0; i < KeysPerThread; ++i)
                        {
                            const bool isLow = ((i & innerStride) == 0u);
                            if (isLow)
                            {
                                const uint partnerIndex = i | innerStride;
                                const bool ascending = ((localKeyStart + i) & crossWaveStepSize) == 0u;
                                const uint threadKey = localKeys[i];
                                const uint partnerKey = localKeys[partnerIndex];
                                localKeys[i] = (ascending == (threadKey < partnerKey)) ? threadKey : partnerKey;
                                localKeys[partnerIndex] = (ascending == (threadKey < partnerKey)) ? partnerKey : threadKey;
                            }
                        }
                    }
                }

                if (localKeyStart < paddedCount)
                {
                    [unroll]
                    for (uint i = 0; i < KeysPerThread; ++i)
                    {
                        SharedSortPixels[localKeyStart + i] = localKeys[i];
                    }
                }

                GroupMemoryBarrierWithGroupSync();

                // all remaining threads have written their local keys back to shared memory
                break;
            }
        }
    }

    // now write back the completed output
    for (uint i = thisLaneGlobal; i < count; i += SortGroupSize)
    {
        const uint pixelIndex = SharedSortPixels[i];
        const float3 position = HealpixPixToVec(pixelIndex, Params.HealpixOrder);
        StoreFrontPointData(begin + i, position, pixelIndex);
    }
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
        const FrontState frontState = LoadFrontStateIn(uint(best.Index));
        const uint frontType = frontState.GetSubduction();
        const float dt = Params.TimeStepMy;

        if (frontType == FrontTypeCollision)
        {
            if (isContinental)
            {
                const float collisionReachKm = 900.0f;
                const float u = clamp(distance / collisionReachKm, 0.0f, 1.0f);
                const float foldProfile = (1.0f - u * u) * (1.0f - u * u);
                const float rate = 0.65f * BaseUplift * foldProfile * frontState.GetStrength();
                relief += rate * dt;

                if (rate > ActiveUplift)
                {
                    crustState.SetOrogenyType(OrogenyHimalayan);
                    crustState.SetOrogenyTime(UpliftConstants.Time);
                }
            }
        }
        else
        {
            const float reach = isContinental ? ContinentalReachKm : IslandArcReachKm;
            const float shape = SubductionProfile(distance, reach);
            const float rate = BaseUplift * shape * frontState.GetStrength();
            relief += rate * dt;

            if (rate > ActiveUplift)
            {
                const int updatedOrogeny = isContinental ? OrogenyAndean : OrogenyIslandArc;
                crustState.SetOrogenyType(updatedOrogeny);
                crustState.SetOrogenyTime(UpliftConstants.Time);
            }
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
