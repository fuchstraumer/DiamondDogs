implementing PlanetLab;
__include Buffers;
__include HEALPix;

// Phase 2: Pure-GPU Continental Collision, Allegretti 2x2 BKE Connected-Component Labeling,
// Terrane Transfer, 1-Dispatch Tiled Suture Distance, Escape Crust Histogram, and Himalayan Surge.
// Reference: Cortial et al. (2019) Section 4.2, Allegretti et al. (IEEE TPDS 2020), tectonics.py Collide/Terrane/TransferTerrane.

static const float TerranePenetrationKm = 300.0f;
static const uint MinCollisionCells = 3u;
static const float CollisionSpeedFixedScale = 1024.0f;
static const uint NoCollisionPair = 0xFFFFFFFFu;

static const float CollisionDistanceKm = 4200.0f;
static const float CollisionCoefficient = 1.3e-5f;
static const float SurgeLimitKm = 4.0f;
static const float SurgeRadiusLimitKm = 1500.0f;
static const float CollisionAreaKept = 1.0f;
static const float HimalayanSurgeThresholdKm = 0.1f;
static const float MinEscapeSearchRadiusKm = 2500.0f;
static const int OrogenyHimalayan = 2;

static const float SuturePositionFixedScale = 16384.0f;
static const uint EscapeHistogramBins = 64u;
static const uint SutureTileSize = 64u;

// CollisionJob word indices (16 uints total).
static const uint JobSlotPlateA = 0u;
static const uint JobSlotPlateB = 1u;
static const uint JobSlotAverageSpeedBits = 2u;
static const uint JobSlotSourcePlate = 3u;
static const uint JobSlotTargetPlate = 4u;
static const uint JobSlotSourceSlot = 5u;
static const uint JobSlotSurgeRadiusBits = 6u;
static const uint JobSlotSurgeAmplitudeBits = 7u;
static const uint JobSlotSourceTerraneCount = 8u;
static const uint JobSlotEscapeRadiusBits = 9u;
static const uint JobSlotSutureCapCenterXBits = 10u;
static const uint JobSlotSutureCapCenterYBits = 11u;
static const uint JobSlotSutureCapCenterZBits = 12u;
static const uint JobSlotSutureCapValid = 13u;
static const uint JobSlotSutureCapSpreadBits = 14u;

// CollideScratchCounters word indices (8 uints total).
static const uint ScratchSlotTerraneCountA = 0u;
static const uint ScratchSlotTerraneCountB = 1u;
static const uint ScratchSlotSutureCount = 2u;
static const uint ScratchSlotReceivedCount = 3u;
static const uint ScratchSlotReserved4 = 4u;
static const uint ScratchSlotReserved5 = 5u;

static const float2 k_ProbeCompass[8] =
{
    float2( 1.0f,  0.0f),
    float2(-1.0f,  0.0f),
    float2( 0.0f,  1.0f),
    float2( 0.0f, -1.0f),
    float2( 0.70710678f,  0.70710678f),
    float2(-0.70710678f,  0.70710678f),
    float2( 0.70710678f, -0.70710678f),
    float2(-0.70710678f, -0.70710678f),
};

// Groupshared storage for Allegretti 2x2 BKE tile-local union-find (8x8 blocks = 64 Morton entries).
groupshared uint SharedBlockMask[64];
groupshared uint SharedBlockParent[64];

// Groupshared storage for suture cap spread, tiled suture distance, and escape histogram.
groupshared uint SharedSutureCapSpread;
groupshared uint SharedGroupInRange;
groupshared float4 SharedSuturePoints[SutureTileSize];
groupshared uint SharedEscapeBins[EscapeHistogramBins];

// -----------------------------------------------------------------------------
// Shared Helpers
// -----------------------------------------------------------------------------

bool FindContinentalOverlap(in OwnersData owners, out uint lowPlate, out uint highPlate)
{
    lowPlate = NoCollisionPair;
    highPlate = NoCollisionPair;

    const int bestPlate = owners.GetBestPlate();
    const int secondPlate = owners.GetSecondPlate();
    if (bestPlate < 0 || secondPlate < 0)
    {
        return false;
    }

    const uint bestFlat = uint(bestPlate) * Params.PointCount + uint(owners.GetBestCell());
    const uint secondFlat = uint(secondPlate) * Params.PointCount + uint(owners.GetSecondCell());
    const CrustFlagBits bestFlags = LoadCrustFlagsIn(bestFlat);
    const CrustFlagBits secondFlags = LoadCrustFlagsIn(secondFlat);
    if (!bestFlags.IsAlive() || !bestFlags.IsContinental() ||
        !secondFlags.IsAlive() || !secondFlags.IsContinental())
    {
        return false;
    }

    lowPlate = uint(min(bestPlate, secondPlate));
    highPlate = uint(max(bestPlate, secondPlate));
    return true;
}

bool IsOverlapDeepEnough(in float3 worldPoint, uint lowPlate, uint highPlate)
{
    const float cellSpacingKm = sqrt(2.0f * HealpixTwoPi / float(Params.PointCount)) * PlanetRadiusKm;
    const float halfPenetrationAngle = 0.5f * (TerranePenetrationKm + cellSpacingKm) / PlanetRadiusKm;

    const float3 referenceAxis = abs(worldPoint.z) < 0.9f
        ? float3(0.0f, 0.0f, 1.0f)
        : float3(1.0f, 0.0f, 0.0f);
    const float3 tangentU = normalize(cross(referenceAxis, worldPoint));
    const float3 tangentV = cross(worldPoint, tangentU);

    [unroll]
    for (uint ring = 0u; ring < 2u; ++ring)
    {
        const float probeAngle = (ring == 0u ? 0.5f : 1.0f) * halfPenetrationAngle;
        float sinAngle = 0.0f;
        float cosAngle = 0.0f;
        sincos(probeAngle, sinAngle, cosAngle);

        [unroll]
        for (uint dirIndex = 0u; dirIndex < 8u; ++dirIndex)
        {
            const float2 compass = k_ProbeCompass[dirIndex];
            const float3 probeDirection = normalize(
                worldPoint * cosAngle + (tangentU * compass.x + tangentV * compass.y) * sinAngle);
            const uint probePoint = HealpixVecToPix(probeDirection, Params.HealpixOrder);
            const OwnersData probeOwners = LoadOwnersIn(probePoint);

            uint probeLow;
            uint probeHigh;
            if (!FindContinentalOverlap(probeOwners, probeLow, probeHigh) ||
                probeLow != lowPlate || probeHigh != highPlate)
            {
                return false;
            }
        }
    }

    return true;
}

uint LocalBlockMorton(uint localBlockX, uint localBlockY)
{
    return SpreadBits16(localBlockX) | (SpreadBits16(localBlockY) << 1u);
}

uint LoadBlockContinentalMask(uint plate, uint firstPixelInBlock)
{
    const uint flatBase = plate * Params.PointCount + firstPixelInBlock;
    if (CRUST_FLAG_BYTES)
    {
        const uint packed4 = CrustFlagsPackedIn.Load(flatBase >> 2u);
        // Bit 0 = alive, bit 1 = continental; bit is 1 iff both alive and continental are set.
        const uint bothBits = packed4 & (packed4 >> 1u) & 0x01010101u;
        return (bothBits & 1u)
             | ((bothBits >> 7u) & 2u)
             | ((bothBits >> 14u) & 4u)
             | ((bothBits >> 21u) & 8u);
    }

    uint blockMask = 0u;
    [unroll]
    for (uint localBit = 0u; localBit < 4u; ++localBit)
    {
        const CrustFlagBits flags = LoadCrustFlagsIn(flatBase + localBit);
        if (flags.IsAlive() && flags.IsContinental())
        {
            blockMask |= (1u << localBit);
        }
    }

    return blockMask;
}

uint FindRootShared(uint mortonIndex)
{
    uint current = mortonIndex;
    while (SharedBlockParent[current] != current)
    {
        current = SharedBlockParent[current];
    }
    return current;
}

void UnionShared(uint indexA, uint indexB)
{
    uint rootA = FindRootShared(indexA);
    uint rootB = FindRootShared(indexB);

    while (rootA != rootB)
    {
        if (rootA < rootB)
        {
            uint previous;
            InterlockedMin(SharedBlockParent[rootB], rootA, previous);
            if (previous == rootB)
            {
                break;
            }
            rootB = previous;
        }
        else
        {
            uint previous;
            InterlockedMin(SharedBlockParent[rootA], rootB, previous);
            if (previous == rootA)
            {
                break;
            }
            rootA = previous;
        }

        rootA = FindRootShared(rootA);
        rootB = FindRootShared(rootB);
    }
}

uint FindRootGlobal(uint globalBlock)
{
    uint current = globalBlock;
    while (true)
    {
        const uint parent = BkeParent.Load(current);
        if (parent == current)
        {
            return current;
        }
        current = parent;
    }
}

void UnionGlobal(uint blockA, uint blockB)
{
    uint rootA = FindRootGlobal(blockA);
    uint rootB = FindRootGlobal(blockB);

    while (rootA != rootB)
    {
        if (rootA < rootB)
        {
            const uint previous = BkeParent.Min(rootB, rootA);
            if (previous == rootB)
            {
                break;
            }
            rootB = previous;
        }
        else
        {
            const uint previous = BkeParent.Min(rootA, rootB);
            if (previous == rootA)
            {
                break;
            }
            rootA = previous;
        }

        rootA = FindRootGlobal(rootA);
        rootB = FindRootGlobal(rootB);
    }
}

// -----------------------------------------------------------------------------
// Stage 0: Zero-Cost Gating (DetectCollisionsCS & SelectCollisionPairCS)
// -----------------------------------------------------------------------------

[shader("compute")]
[numthreads(64, 1, 1)]
void DetectCollisionsCS(uint3 threadId : SV_DispatchThreadID)
{
    const uint worldPointIndex = threadId.x;
    if (worldPointIndex >= Params.PointCount)
    {
        return;
    }

    const OwnersData owners = LoadOwnersIn(worldPointIndex);
    uint lowPlate;
    uint highPlate;
    if (!FindContinentalOverlap(owners, lowPlate, highPlate))
    {
        return;
    }

    const uint pairIndex = lowPlate * Params.PlateCount + highPlate;
    const float4 poleRatesLow = LoadPoleRates(lowPlate);
    const float4 poleRatesHigh = LoadPoleRates(highPlate);
    const float3 relativeSpin = (poleRatesLow.xyz * poleRatesLow.w) - (poleRatesHigh.xyz * poleRatesHigh.w);
    const float3 worldPoint = HealpixPixToVec(worldPointIndex, Params.HealpixOrder);
    const float convergenceSpeed = length(cross(relativeSpin, worldPoint)) * PlanetRadiusKm;

    CollisionPairCount.Add(pairIndex, 1u);
    CollisionPairSpeedSum.Add(pairIndex, uint(round(convergenceSpeed * CollisionSpeedFixedScale)));

    if (IsOverlapDeepEnough(worldPoint, lowPlate, highPlate))
    {
        CollisionPairDeepCount.Add(pairIndex, 1u);
    }
}

[shader("compute")]
[numthreads(32, 1, 1)]
void SelectCollisionPairCS(uint3 laneInGroup : SV_GroupThreadID)
{
    const uint lane = laneInGroup.x;
    const uint totalPairs = Params.PlateCount * Params.PlateCount;

    uint localBestPair = NoCollisionPair;
    for (uint pairIndex = lane; pairIndex < totalPairs; pairIndex += 32u)
    {
        const uint overlapCount = CollisionPairCountIn.Load(pairIndex);
        const uint deepCount = CollisionPairDeepCountIn.Load(pairIndex);
        if (overlapCount >= MinCollisionCells && deepCount > 0u)
        {
            localBestPair = min(localBestPair, pairIndex);
        }
    }

    const uint winningPair = WaveActiveMin(localBestPair);

    // Clear per-collision histogram & scratch counters only when a collision actually triggers.
    if (winningPair != NoCollisionPair)
    {
        for (uint bin = lane; bin < EscapeHistogramBins; bin += 32u)
        {
            EscapeHistogram.Store(bin, 0u);
        }
    }

    if (lane != 0u)
    {
        return;
    }

    if (winningPair == NoCollisionPair)
    {
        CollisionJob.Store(JobSlotPlateA, NoCollisionPair);
        CollisionJob.Store(JobSlotPlateB, NoCollisionPair);
        [unroll]
        for (uint slot = 0u; slot < 4u; ++slot)
        {
            CollideDispatch.Store(slot * 3u + 0u, 0u);
            CollideDispatch.Store(slot * 3u + 1u, 1u);
            CollideDispatch.Store(slot * 3u + 2u, 1u);
        }
        return;
    }

    const uint plateA = winningPair / Params.PlateCount;
    const uint plateB = winningPair % Params.PlateCount;
    const uint overlapCount = max(CollisionPairCountIn.Load(winningPair), 1u);
    const float averageSpeed = (float(CollisionPairSpeedSumIn.Load(winningPair)) / CollisionSpeedFixedScale)
                             / float(overlapCount);

    CollisionJob.Store(JobSlotPlateA, plateA);
    CollisionJob.Store(JobSlotPlateB, plateB);
    CollisionJob.Store(JobSlotAverageSpeedBits, asuint(averageSpeed));
    CollisionJob.Store(JobSlotSutureCapValid, 0u);
    CollisionJob.Store(JobSlotEscapeRadiusBits, asuint(-1.0f));

    [unroll]
    for (uint counterIndex = 0u; counterIndex < 8u; ++counterIndex)
    {
        CollideScratchCounters.Store(counterIndex, 0u);
    }

    SuturePositionSum.Store(0u, 0);
    SuturePositionSum.Store(1u, 0);
    SuturePositionSum.Store(2u, 0);

    const uint tilesPerPlate = Params.PointCount >> 8u;
    const uint blocksPerPlate = Params.PointCount >> 2u;
    const uint tileGroups = 2u * tilesPerPlate;
    const uint blockGroups = ((2u * blocksPerPlate) + 63u) / 64u;
    const uint cellGroups = (Params.PointCount + 63u) / 64u;

    CollideDispatch.Store(0u, tileGroups);
    CollideDispatch.Store(1u, 1u);
    CollideDispatch.Store(2u, 1u);

    CollideDispatch.Store(3u, blockGroups);
    CollideDispatch.Store(4u, 1u);
    CollideDispatch.Store(5u, 1u);

    CollideDispatch.Store(6u, cellGroups);
    CollideDispatch.Store(7u, 1u);
    CollideDispatch.Store(8u, 1u);

    CollideDispatch.Store(9u, 1u);
    CollideDispatch.Store(10u, 1u);
    CollideDispatch.Store(11u, 1u);
}

// -----------------------------------------------------------------------------
// Stage 1-3: Allegretti 2x2 BKE Connected-Component Labeling on 12 HEALPix Faces
// -----------------------------------------------------------------------------

[shader("compute")]
[numthreads(8, 8, 1)]
void BkeTileInitAndLocalUnionCS(uint3 groupId : SV_GroupID, uint3 localId : SV_GroupThreadID)
{
    const uint tilesPerPlate = Params.PointCount >> 8u;
    const uint slot = groupId.x / tilesPerPlate;
    const uint tileIndex = groupId.x % tilesPerPlate;
    const uint plate = CollisionJobIn.Load(slot == 0u ? JobSlotPlateA : JobSlotPlateB);

    const uint localBlockX = localId.x;
    const uint localBlockY = localId.y;
    const uint localMorton = LocalBlockMorton(localBlockX, localBlockY);
    const uint blockInPlate = (tileIndex << 6u) | localMorton;
    const uint firstPixelInBlock = blockInPlate << 2u;

    const uint blockMask = LoadBlockContinentalMask(plate, firstPixelInBlock);
    SharedBlockMask[localMorton] = blockMask;
    SharedBlockParent[localMorton] = (blockMask != 0u) ? localMorton : NoCollisionPair;

    GroupMemoryBarrierWithGroupSync();

    if (blockMask != 0u)
    {
        if (localBlockX > 0u)
        {
            const uint westMorton = LocalBlockMorton(localBlockX - 1u, localBlockY);
            const uint westMask = SharedBlockMask[westMorton];
            if (((blockMask & 5u) != 0u) && ((westMask & 10u) != 0u))
            {
                UnionShared(localMorton, westMorton);
            }
        }

        if (localBlockY > 0u)
        {
            const uint southMorton = LocalBlockMorton(localBlockX, localBlockY - 1u);
            const uint southMask = SharedBlockMask[southMorton];
            if (((blockMask & 3u) != 0u) && ((southMask & 12u) != 0u))
            {
                UnionShared(localMorton, southMorton);
            }
        }

        if (localBlockX > 0u && localBlockY > 0u)
        {
            const uint southWestMorton = LocalBlockMorton(localBlockX - 1u, localBlockY - 1u);
            const uint southWestMask = SharedBlockMask[southWestMorton];
            if (((blockMask & 1u) != 0u) && ((southWestMask & 8u) != 0u))
            {
                UnionShared(localMorton, southWestMorton);
            }
        }

        if (localBlockX > 0u && localBlockY < 7u)
        {
            const uint northWestMorton = LocalBlockMorton(localBlockX - 1u, localBlockY + 1u);
            const uint northWestMask = SharedBlockMask[northWestMorton];
            if (((blockMask & 4u) != 0u) && ((northWestMask & 2u) != 0u))
            {
                UnionShared(localMorton, northWestMorton);
            }
        }
    }

    GroupMemoryBarrierWithGroupSync();

    const uint blocksPerPlate = Params.PointCount >> 2u;
    const uint slotBase = slot * blocksPerPlate;
    const uint globalBlock = slotBase + blockInPlate;

    BkeMask.Store(globalBlock, blockMask);
    BkeSeedFlag.Store(globalBlock, 0u);

    if (blockMask != 0u)
    {
        const uint localRootMorton = FindRootShared(localMorton);
        const uint globalRootBlock = slotBase + ((tileIndex << 6u) | localRootMorton);
        BkeParent.Store(globalBlock, globalRootBlock);
    }
    else
    {
        BkeParent.Store(globalBlock, NoCollisionPair);
    }
}

[shader("compute")]
[numthreads(8, 8, 1)]
void BkeStitchSeamsCS(uint3 groupId : SV_GroupID, uint3 localId : SV_GroupThreadID)
{
    const uint localBlockX = localId.x;
    const uint localBlockY = localId.y;
    if (localBlockX > 0u && localBlockX < 7u && localBlockY > 0u && localBlockY < 7u)
    {
        return;
    }

    const uint tilesPerPlate = Params.PointCount >> 8u;
    const uint blocksPerPlate = Params.PointCount >> 2u;
    const uint slot = groupId.x / tilesPerPlate;
    const uint tileIndex = groupId.x % tilesPerPlate;
    const uint slotBase = slot * blocksPerPlate;

    const uint localMorton = LocalBlockMorton(localBlockX, localBlockY);
    const uint blockInPlate = (tileIndex << 6u) | localMorton;
    const uint globalBlock = slotBase + blockInPlate;
    const uint blockMask = BkeMask.Load(globalBlock);
    if (blockMask == 0u)
    {
        return;
    }

    const uint order = Params.HealpixOrder;
    const uint sideLength = 1u << order;
    const uint blocksPerSide = sideLength >> 1u;
    const uint firstPixelInBlock = blockInPlate << 2u;

    uint baseFace;
    uint pixelBaseX;
    uint pixelBaseY;
    HealpixDecode(firstPixelInBlock, order, baseFace, pixelBaseX, pixelBaseY);

    const uint blockX = pixelBaseX >> 1u;
    const uint blockY = pixelBaseY >> 1u;

    // 1. Interior 16x16 tile borders within the same HEALPix base face.
    if (localBlockX == 0u && blockX > 0u)
    {
        const uint westBlock = HealpixEncode(baseFace, (blockX - 1u) << 1u, blockY << 1u, order) >> 2u;
        const uint westMask = BkeMask.Load(slotBase + westBlock);
        if (((blockMask & 5u) != 0u) && ((westMask & 10u) != 0u))
        {
            UnionGlobal(globalBlock, slotBase + westBlock);
        }
    }

    if (localBlockY == 0u && blockY > 0u)
    {
        const uint southBlock = HealpixEncode(baseFace, blockX << 1u, (blockY - 1u) << 1u, order) >> 2u;
        const uint southMask = BkeMask.Load(slotBase + southBlock);
        if (((blockMask & 3u) != 0u) && ((southMask & 12u) != 0u))
        {
            UnionGlobal(globalBlock, slotBase + southBlock);
        }
    }

    if ((localBlockX == 0u || localBlockY == 0u) && blockX > 0u && blockY > 0u)
    {
        const uint southWestBlock = HealpixEncode(baseFace, (blockX - 1u) << 1u, (blockY - 1u) << 1u, order) >> 2u;
        const uint southWestMask = BkeMask.Load(slotBase + southWestBlock);
        if (((blockMask & 1u) != 0u) && ((southWestMask & 8u) != 0u))
        {
            UnionGlobal(globalBlock, slotBase + southWestBlock);
        }
    }

    if ((localBlockX == 0u || localBlockY == 7u) && blockX > 0u && (blockY + 1u) < blocksPerSide)
    {
        const uint northWestBlock = HealpixEncode(baseFace, (blockX - 1u) << 1u, (blockY + 1u) << 1u, order) >> 2u;
        const uint northWestMask = BkeMask.Load(slotBase + northWestBlock);
        if (((blockMask & 4u) != 0u) && ((northWestMask & 2u) != 0u))
        {
            UnionGlobal(globalBlock, slotBase + northWestBlock);
        }
    }

    // 2. Base-face seam borders: resolve via exact analytic HealpixNeighbor (handles rotations, poles, 24 corners).
    const bool onBaseFacePerimeter =
        (blockX == 0u) || (blockY == 0u) ||
        (blockX + 1u == blocksPerSide) || (blockY + 1u == blocksPerSide);
    if (!onBaseFacePerimeter)
    {
        return;
    }

    const int sideLengthInt = int(sideLength);
    [unroll]
    for (uint localBit = 0u; localBit < 4u; ++localBit)
    {
        if ((blockMask & (1u << localBit)) == 0u)
        {
            continue;
        }

        const int pixelX = int(pixelBaseX + (localBit & 1u));
        const int pixelY = int(pixelBaseY + (localBit >> 1u));
        const bool pixelOnFaceEdge =
            (pixelX == 0) || (pixelY == 0) ||
            (pixelX == sideLengthInt - 1) || (pixelY == sideLengthInt - 1);
        if (!pixelOnFaceEdge)
        {
            continue;
        }

        [unroll]
        for (uint dirIndex = 0u; dirIndex < 8u; ++dirIndex)
        {
            const int deltaX = k_NeighborDeltaX[dirIndex];
            const int deltaY = k_NeighborDeltaY[dirIndex];
            const int steppedX = pixelX + deltaX;
            const int steppedY = pixelY + deltaY;
            const bool crossesFaceSeam =
                (steppedX < 0) || (steppedX >= sideLengthInt) ||
                (steppedY < 0) || (steppedY >= sideLengthInt);
            if (!crossesFaceSeam)
            {
                continue;
            }

            const int neighborPixel = HealpixNeighbor(baseFace, pixelX, pixelY, deltaX, deltaY, order);
            if (neighborPixel < 0)
            {
                continue;
            }

            const uint neighborBlock = uint(neighborPixel) >> 2u;
            const uint neighborBit = uint(neighborPixel) & 3u;
            const uint neighborMask = BkeMask.Load(slotBase + neighborBlock);
            if ((neighborMask & (1u << neighborBit)) != 0u)
            {
                UnionGlobal(globalBlock, slotBase + neighborBlock);
            }
        }
    }
}

[shader("compute")]
[numthreads(64, 1, 1)]
void BkeFlattenCS(uint3 threadId : SV_DispatchThreadID)
{
    const uint totalBlocks = 2u * (Params.PointCount >> 2u);
    const uint globalBlock = threadId.x;
    if (globalBlock >= totalBlocks)
    {
        return;
    }

    if (BkeMask.Load(globalBlock) == 0u)
    {
        return;
    }

    const uint root = FindRootGlobal(globalBlock);
    BkeParent.Store(globalBlock, root);
}

// -----------------------------------------------------------------------------
// Stage 4: Terrane Seed Marking, Counting, and Source/Target Selection
// -----------------------------------------------------------------------------

[shader("compute")]
[numthreads(64, 1, 1)]
void MarkTerraneSeedsCS(uint3 threadId : SV_DispatchThreadID)
{
    const uint worldPointIndex = threadId.x;
    if (worldPointIndex >= Params.PointCount)
    {
        return;
    }

    const uint plateA = CollisionJobIn.Load(JobSlotPlateA);
    const uint plateB = CollisionJobIn.Load(JobSlotPlateB);
    const OwnersData owners = LoadOwnersIn(worldPointIndex);

    uint lowPlate;
    uint highPlate;
    if (!FindContinentalOverlap(owners, lowPlate, highPlate) ||
        lowPlate != plateA || highPlate != plateB)
    {
        return;
    }

    const bool bestIsPlateA = uint(owners.GetBestPlate()) == plateA;
    const uint cellA = uint(bestIsPlateA ? owners.GetBestCell() : owners.GetSecondCell());
    const uint cellB = uint(bestIsPlateA ? owners.GetSecondCell() : owners.GetBestCell());

    const uint blocksPerPlate = Params.PointCount >> 2u;
    const uint rootA = BkeParent.Load(cellA >> 2u);
    const uint rootB = BkeParent.Load(blocksPerPlate + (cellB >> 2u));

    if (rootA != NoCollisionPair)
    {
        BkeSeedFlag.Store(rootA, 1u);
    }

    if (rootB != NoCollisionPair)
    {
        BkeSeedFlag.Store(rootB, 1u);
    }
}

[shader("compute")]
[numthreads(64, 1, 1)]
void CountTerraneCS(uint3 threadId : SV_DispatchThreadID)
{
    const uint blocksPerPlate = Params.PointCount >> 2u;
    const uint totalBlocks = 2u * blocksPerPlate;
    const uint globalBlock = threadId.x;

    uint localTerraneCells = 0u;
    if (globalBlock < totalBlocks)
    {
        const uint blockMask = BkeMask.Load(globalBlock);
        if (blockMask != 0u)
        {
            const uint root = BkeParent.Load(globalBlock);
            if (root != NoCollisionPair && BkeSeedFlag.Load(root) != 0u)
            {
                localTerraneCells = countbits(blockMask);
            }
        }
    }

    // blocksPerPlate is a multiple of 64 for order >= 4, so every wave belongs to one slot.
    const uint slot = (globalBlock < blocksPerPlate) ? ScratchSlotTerraneCountA : ScratchSlotTerraneCountB;
    const uint waveTotal = WaveActiveSum(localTerraneCells);
    if (WaveIsFirstLane() && waveTotal > 0u)
    {
        CollideScratchCounters.Add(slot, waveTotal);
    }
}

[shader("compute")]
[numthreads(32, 1, 1)]
void DecideTransferCS(uint3 laneInGroup : SV_GroupThreadID)
{
    if (laneInGroup.x != 0u)
    {
        return;
    }

    const uint plateA = CollisionJob.Load(JobSlotPlateA);
    const uint plateB = CollisionJob.Load(JobSlotPlateB);
    const float averageSpeed = asfloat(CollisionJob.Load(JobSlotAverageSpeedBits));
    const uint terraneCountA = CollideScratchCounters.Load(ScratchSlotTerraneCountA);
    const uint terraneCountB = CollideScratchCounters.Load(ScratchSlotTerraneCountB);

    const bool transferPlateA = (terraneCountA <= terraneCountB);
    const uint sourcePlate = transferPlateA ? plateA : plateB;
    const uint targetPlate = transferPlateA ? plateB : plateA;
    const uint sourceSlot = transferPlateA ? 0u : 1u;
    const uint sourceTerraneCount = transferPlateA ? terraneCountA : terraneCountB;

    const float planetSurfaceAreaKm2 = 2.0f * HealpixTwoPi * PlanetRadiusKm * PlanetRadiusKm;
    const float terraneAreaKm2 = float(sourceTerraneCount) * planetSurfaceAreaKm2 / float(Params.PointCount);
    const float areaOverMeanPlate = (float(sourceTerraneCount) * float(Params.PlateCount)) / float(Params.PointCount);
    const float rawSurgeRadius = CollisionDistanceKm
                               * sqrt(max(0.0f, (averageSpeed / MaximumPlateSpeed) * areaOverMeanPlate));
    const float surgeRadiusKm = clamp(rawSurgeRadius, 1.0f, SurgeRadiusLimitKm);
    const float surgeAmplitudeKm = min(CollisionCoefficient * terraneAreaKm2, SurgeLimitKm);

    CollisionJob.Store(JobSlotSourcePlate, sourcePlate);
    CollisionJob.Store(JobSlotTargetPlate, targetPlate);
    CollisionJob.Store(JobSlotSourceSlot, sourceSlot);
    CollisionJob.Store(JobSlotSurgeRadiusBits, asuint(surgeRadiusKm));
    CollisionJob.Store(JobSlotSurgeAmplitudeBits, asuint(surgeAmplitudeKm));
    CollisionJob.Store(JobSlotSourceTerraneCount, sourceTerraneCount);
}

// -----------------------------------------------------------------------------
// Stage 5-8: Terrane Transfer, Suture Distance, Escape Crust, and Himalayan Surge
// -----------------------------------------------------------------------------

[shader("compute")]
[numthreads(64, 1, 1)]
void TransferTerraneDirectCS(uint3 threadId : SV_DispatchThreadID)
{
    const uint targetCell = threadId.x;
    const bool inRange = targetCell < Params.PointCount;

    const uint sourcePlate = CollisionJobIn.Load(JobSlotSourcePlate);
    const uint targetPlate = CollisionJobIn.Load(JobSlotTargetPlate);
    const uint sourceSlot = CollisionJobIn.Load(JobSlotSourceSlot);
    const uint blocksPerPlate = Params.PointCount >> 2u;

    uint didTransfer = 0u;
    int3 anchorPositionFixed = int3(0, 0, 0);

    if (inRange)
    {
        const float3x4 targetRotation = LoadRotation(targetPlate);
        const float3x4 sourceRotation = LoadRotation(sourcePlate);
        const float3 targetLocalPos = HealpixPixToVec(targetCell, Params.HealpixOrder);
        const float3 worldPos = PlateToWorld(targetLocalPos, targetRotation);
        const float3 sourceLocalPos = WorldToPlate(worldPos, sourceRotation);
        const uint sourceCell = HealpixVecToPix(sourceLocalPos, Params.HealpixOrder);

        const uint sourceBlock = sourceSlot * blocksPerPlate + (sourceCell >> 2u);
        const uint sourceMask = BkeMask.Load(sourceBlock);
        const bool isSourceContinental = (sourceMask & (1u << (sourceCell & 3u))) != 0u;

        bool inSourceTerrane = false;
        if (isSourceContinental)
        {
            const uint sourceRoot = BkeParent.Load(sourceBlock);
            inSourceTerrane = (sourceRoot != NoCollisionPair) && (BkeSeedFlag.Load(sourceRoot) != 0u);
        }

        if (inSourceTerrane)
        {
            const uint targetFlat = targetPlate * Params.PointCount + targetCell;
            const CrustFlagBits targetFlags = LoadCrustFlags(targetFlat);
            if (targetFlags.IsAlive() && targetFlags.IsContinental())
            {
                const uint anchorSlot = CollideScratchCounters.Add(ScratchSlotSutureCount, 1u);
                SuturePoints.Store(anchorSlot, float4(targetLocalPos, 0.0f));
                anchorPositionFixed = int3(round(targetLocalPos * SuturePositionFixedScale));
            }
            else
            {
                const uint sourceFlat = sourcePlate * Params.PointCount + sourceCell;
                StoreCrustFlags(targetFlat, PackCrustFlags(true, true));
                StoreCrustState(targetFlat, LoadCrustState(sourceFlat));
                didTransfer = 1u;
            }
        }
    }

    const uint waveTransferred = WaveActiveSum(didTransfer);
    const int waveSumX = WaveActiveSum(anchorPositionFixed.x);
    const int waveSumY = WaveActiveSum(anchorPositionFixed.y);
    const int waveSumZ = WaveActiveSum(anchorPositionFixed.z);

    if (WaveIsFirstLane())
    {
        if (waveTransferred > 0u)
        {
            CollideScratchCounters.Add(ScratchSlotReceivedCount, waveTransferred);
        }

        if (waveSumX != 0)
        {
            SuturePositionSum.Add(0u, waveSumX);
        }

        if (waveSumY != 0)
        {
            SuturePositionSum.Add(1u, waveSumY);
        }

        if (waveSumZ != 0)
        {
            SuturePositionSum.Add(2u, waveSumZ);
        }
    }
}

[shader("compute")]
[numthreads(64, 1, 1)]
void ClearSourceTerraneCS(uint3 threadId : SV_DispatchThreadID)
{
    const uint sourceCell = threadId.x;
    if (sourceCell >= Params.PointCount)
    {
        return;
    }

    const uint sourcePlate = CollisionJob.Load(JobSlotSourcePlate);
    const uint sourceSlot = CollisionJob.Load(JobSlotSourceSlot);
    const uint blocksPerPlate = Params.PointCount >> 2u;

    const uint sourceBlock = sourceSlot * blocksPerPlate + (sourceCell >> 2u);
    const uint sourceMask = BkeMask.Load(sourceBlock);
    if ((sourceMask & (1u << (sourceCell & 3u))) != 0u)
    {
        const uint sourceRoot = BkeParent.Load(sourceBlock);
        if (sourceRoot != NoCollisionPair && BkeSeedFlag.Load(sourceRoot) != 0u)
        {
            const uint sourceFlat = sourcePlate * Params.PointCount + sourceCell;
            StoreCrustFlags(sourceFlat, PackCrustFlags(false, false));
        }
    }

    // Thread 0 normalizes the suture anchor centroid for cap-culling NearestSuturePointCS.
    if (sourceCell == 0u)
    {
        const uint sutureCount = CollideScratchCounters.Load(ScratchSlotSutureCount);
        if (sutureCount > 0u)
        {
            const float3 positionSum = float3(
                float(SuturePositionSum.Load(0u)),
                float(SuturePositionSum.Load(1u)),
                float(SuturePositionSum.Load(2u)));
            const float sumLength = length(positionSum);
            if (sumLength >= 1.0e-3f * SuturePositionFixedScale * float(sutureCount))
            {
                const float3 capCenter = positionSum / sumLength;
                CollisionJob.Store(JobSlotSutureCapCenterXBits, asuint(capCenter.x));
                CollisionJob.Store(JobSlotSutureCapCenterYBits, asuint(capCenter.y));
                CollisionJob.Store(JobSlotSutureCapCenterZBits, asuint(capCenter.z));
                CollisionJob.Store(JobSlotSutureCapValid, 1u);
            }
            else
            {
                CollisionJob.Store(JobSlotSutureCapValid, 0u);
            }
        }
        else
        {
            CollisionJob.Store(JobSlotSutureCapValid, 0u);
        }
    }
}

[shader("compute")]
[numthreads(256, 1, 1)]
void ComputeSutureCapSpreadCS(uint3 laneInGroup : SV_GroupThreadID)
{
    const uint lane = laneInGroup.x;
    if (lane == 0u)
    {
        SharedSutureCapSpread = 0u;
    }

    GroupMemoryBarrierWithGroupSync();

    const uint sutureCount = CollideScratchCounters.Load(ScratchSlotSutureCount);
    const uint capValid = CollisionJob.Load(JobSlotSutureCapValid);
    float localMaxSpread = 0.0f;

    if (sutureCount > 0u && capValid != 0u)
    {
        const float3 capCenter = float3(
            asfloat(CollisionJob.Load(JobSlotSutureCapCenterXBits)),
            asfloat(CollisionJob.Load(JobSlotSutureCapCenterYBits)),
            asfloat(CollisionJob.Load(JobSlotSutureCapCenterZBits)));

        for (uint anchorIndex = lane; anchorIndex < sutureCount; anchorIndex += 256u)
        {
            const float3 anchorPos = SuturePoints.Load(anchorIndex).xyz;
            localMaxSpread = max(localMaxSpread, 1.0f - dot(anchorPos, capCenter));
        }
    }

    const uint waveMaxSpread = WaveActiveMax(asuint(localMaxSpread));
    if (WaveIsFirstLane())
    {
        InterlockedMax(SharedSutureCapSpread, waveMaxSpread);
    }

    GroupMemoryBarrierWithGroupSync();

    if (lane == 0u)
    {
        CollisionJob.Store(JobSlotSutureCapSpreadBits, SharedSutureCapSpread);
    }
}

[shader("compute")]
[numthreads(SutureTileSize, 1, 1)]
void NearestSuturePointCS(uint3 threadId : SV_DispatchThreadID, uint3 laneInGroup : SV_GroupThreadID)
{
    const uint targetCell = threadId.x;
    const uint lane = laneInGroup.x;
    const bool inRange = targetCell < Params.PointCount;

    const uint sutureCount = CollideScratchCounters.Load(ScratchSlotSutureCount);
    if (sutureCount == 0u)
    {
        if (inRange)
        {
            SutureDistance.Store(targetCell, 1.0e9f);
        }
        return;
    }

    const float surgeRadiusKm = asfloat(CollisionJobIn.Load(JobSlotSurgeRadiusBits));
    const float searchRadiusKm = max(surgeRadiusKm, MinEscapeSearchRadiusKm);
    const float3 targetPos = inRange ? HealpixPixToVec(targetCell, Params.HealpixOrder) : float3(0.0f, 0.0f, 1.0f);

    bool cellInRange = inRange;
    const uint capValid = CollisionJobIn.Load(JobSlotSutureCapValid);
    if (inRange && capValid != 0u)
    {
        const float3 capCenter = float3(
            asfloat(CollisionJobIn.Load(JobSlotSutureCapCenterXBits)),
            asfloat(CollisionJobIn.Load(JobSlotSutureCapCenterYBits)),
            asfloat(CollisionJobIn.Load(JobSlotSutureCapCenterZBits)));
        const float capSpread = asfloat(CollisionJobIn.Load(JobSlotSutureCapSpreadBits));
        const float capCos = clamp(1.0f - capSpread, -1.0f, 1.0f);
        const float capSin = sqrt(max(0.0f, 1.0f - capCos * capCos));

        float searchSin = 0.0f;
        float searchCos = 0.0f;
        sincos(searchRadiusKm / PlanetRadiusKm, searchSin, searchCos);

        const bool wrapsPi = (capCos < 0.0f) && (capSin < searchSin);
        const float minCos = wrapsPi ? -1.0f : (capCos * searchCos - capSin * searchSin);
        if (dot(targetPos, capCenter) < minCos)
        {
            cellInRange = false;
        }
    }

    if (lane == 0u)
    {
        SharedGroupInRange = 0u;
    }

    GroupMemoryBarrierWithGroupSync();

    if (cellInRange)
    {
        SharedGroupInRange = 1u;
    }

    GroupMemoryBarrierWithGroupSync();

    if (SharedGroupInRange == 0u)
    {
        if (inRange)
        {
            SutureDistance.Store(targetCell, 1.0e9f);
        }
        return;
    }

    float bestDot = -2.0f;
    for (uint tileBase = 0u; tileBase < sutureCount; tileBase += SutureTileSize)
    {
        const uint loadIndex = tileBase + lane;
        SharedSuturePoints[lane] = (loadIndex < sutureCount) ? SuturePoints.Load(loadIndex) : float4(0.0f, 0.0f, 0.0f, 0.0f);

        GroupMemoryBarrierWithGroupSync();

        if (cellInRange)
        {
            const uint tileEnd = min(SutureTileSize, sutureCount - tileBase);
            for (uint offset = 0u; offset < tileEnd; ++offset)
            {
                bestDot = max(bestDot, dot(targetPos, SharedSuturePoints[offset].xyz));
            }
        }

        GroupMemoryBarrierWithGroupSync();
    }

    if (inRange)
    {
        const float distanceKm = cellInRange
            ? (PlanetRadiusKm * acos(clamp(bestDot, -1.0f, 1.0f)))
            : 1.0e9f;
        SutureDistance.Store(targetCell, distanceKm);
    }
}

[shader("compute")]
[numthreads(EscapeHistogramBins, 1, 1)]
void EscapeHistogramCS(uint3 threadId : SV_DispatchThreadID, uint3 laneInGroup : SV_GroupThreadID)
{
    const uint lane = laneInGroup.x;
    const uint sutureCount = CollideScratchCounters.Load(ScratchSlotSutureCount);
    const uint sourceTerraneCount = CollisionJobIn.Load(JobSlotSourceTerraneCount);
    const uint receivedCount = CollideScratchCounters.Load(ScratchSlotReceivedCount);
    const int rawDeficit = int(sourceTerraneCount) - int(receivedCount);

    if (sutureCount == 0u || rawDeficit <= 0)
    {
        return;
    }

    SharedEscapeBins[lane] = 0u;
    GroupMemoryBarrierWithGroupSync();

    const uint targetCell = threadId.x;
    if (targetCell < Params.PointCount)
    {
        const uint targetPlate = CollisionJobIn.Load(JobSlotTargetPlate);
        const CrustFlagBits targetFlags = LoadCrustFlagsIn(targetPlate * Params.PointCount + targetCell);
        const bool isFree = !(targetFlags.IsAlive() && targetFlags.IsContinental());

        if (isFree)
        {
            const float surgeRadiusKm = asfloat(CollisionJobIn.Load(JobSlotSurgeRadiusBits));
            const float searchRadiusKm = max(surgeRadiusKm, MinEscapeSearchRadiusKm);
            const float distanceKm = SutureDistance.Load(targetCell);
            if (distanceKm < searchRadiusKm)
            {
                const uint bin = min(uint(floor((distanceKm / searchRadiusKm) * float(EscapeHistogramBins))),
                                     EscapeHistogramBins - 1u);
                InterlockedAdd(SharedEscapeBins[bin], 1u);
            }
        }
    }

    GroupMemoryBarrierWithGroupSync();

    const uint binCount = SharedEscapeBins[lane];
    if (binCount > 0u)
    {
        EscapeHistogram.Add(lane, binCount);
    }
}

[shader("compute")]
[numthreads(EscapeHistogramBins, 1, 1)]
void EscapePrefixSumCS(uint3 laneInGroup : SV_GroupThreadID)
{
    const uint lane = laneInGroup.x;
    SharedEscapeBins[lane] = EscapeHistogram.Load(lane);
    GroupMemoryBarrierWithGroupSync();

    if (lane != 0u)
    {
        return;
    }

    const uint sutureCount = CollideScratchCounters.Load(ScratchSlotSutureCount);
    const uint sourceTerraneCount = CollisionJob.Load(JobSlotSourceTerraneCount);
    const uint receivedCount = CollideScratchCounters.Load(ScratchSlotReceivedCount);
    const int rawDeficit = int(sourceTerraneCount) - int(receivedCount);
    const uint deficit = (sutureCount > 0u && rawDeficit > 0)
        ? uint(round(CollisionAreaKept * float(rawDeficit)))
        : 0u;

    if (deficit == 0u)
    {
        CollisionJob.Store(JobSlotEscapeRadiusBits, asuint(-1.0f));
        return;
    }

    const float surgeRadiusKm = asfloat(CollisionJob.Load(JobSlotSurgeRadiusBits));
    const float searchRadiusKm = max(surgeRadiusKm, MinEscapeSearchRadiusKm);
    const float binWidthKm = searchRadiusKm / float(EscapeHistogramBins);

    uint cumulative = 0u;
    float escapeRadiusKm = searchRadiusKm;
    for (uint bin = 0u; bin < EscapeHistogramBins; ++bin)
    {
        const uint binCount = SharedEscapeBins[bin];
        const uint nextCumulative = cumulative + binCount;
        if (nextCumulative >= deficit)
        {
            const float fraction = (binCount > 0u)
                ? clamp(float(deficit - cumulative) / float(binCount), 0.0f, 1.0f)
                : 1.0f;
            escapeRadiusKm = (float(bin) + fraction) * binWidthKm;
            break;
        }
        cumulative = nextCumulative;
    }

    CollisionJob.Store(JobSlotEscapeRadiusBits, asuint(escapeRadiusKm));
}

[shader("compute")]
[numthreads(64, 1, 1)]
void ApplyEscapeAndSurgeCS(uint3 threadId : SV_DispatchThreadID)
{
    const uint targetCell = threadId.x;
    if (targetCell >= Params.PointCount)
    {
        return;
    }

    const uint sutureCount = CollideScratchCounters.Load(ScratchSlotSutureCount);
    if (sutureCount == 0u)
    {
        return;
    }

    const uint targetPlate = CollisionJobIn.Load(JobSlotTargetPlate);
    const uint targetFlat = targetPlate * Params.PointCount + targetCell;
    const float distanceKm = SutureDistance.Load(targetCell);
    const float escapeRadiusKm = asfloat(CollisionJobIn.Load(JobSlotEscapeRadiusBits));
    const float surgeRadiusKm = asfloat(CollisionJobIn.Load(JobSlotSurgeRadiusBits));
    const float surgeAmplitudeKm = asfloat(CollisionJobIn.Load(JobSlotSurgeAmplitudeBits));

    const CrustFlagBits targetFlags = LoadCrustFlags(targetFlat);
    bool isContinental = targetFlags.IsAlive() && targetFlags.IsContinental();

    if (!isContinental && escapeRadiusKm >= 0.0f && distanceKm <= escapeRadiusKm)
    {
        StoreCrustFlags(targetFlat, PackCrustFlags(true, true));
        StoreCrustState(targetFlat, 0.0f, Pass3Constants.Time, Pass3Constants.Time, 0);
        isContinental = true;
    }

    if (isContinental && distanceKm < surgeRadiusKm)
    {
        const float ratio = clamp(distanceKm / surgeRadiusKm, 0.0f, 1.0f);
        const float oneMinusRatioSquared = 1.0f - ratio * ratio;
        const float surgeKm = surgeAmplitudeKm * oneMinusRatioSquared * oneMinusRatioSquared;

        CrustStateData crustState = LoadCrustState(targetFlat);
        crustState.SetRelief(min(crustState.GetRelief() + surgeKm, PeakElevation));
        if (surgeKm > HimalayanSurgeThresholdKm)
        {
            crustState.SetOrogenyType(OrogenyHimalayan);
            crustState.SetOrogenyTime(Pass3Constants.Time);
        }
        StoreCrustState(targetFlat, crustState);
    }
}

[shader("compute")]
[numthreads(64, 1, 1)]
void RefreshOwnersAfterCollisionCS(uint3 threadId : SV_DispatchThreadID)
{
    const uint worldPointIndex = threadId.x;
    if (worldPointIndex >= Params.PointCount)
    {
        return;
    }

    const int sourcePlate = int(CollisionJobIn.Load(JobSlotSourcePlate));
    const int targetPlate = int(CollisionJobIn.Load(JobSlotTargetPlate));

    if (worldPointIndex == 0u)
    {
        // Invalidate both colliding plates' bounding caps so CountLiveCellsCS scans them in full
        // and rebuilds tight caps covering the newly transferred terrane and escape crust.
        PlateCapCenters.Store(uint(sourcePlate), float4(0.0f, 0.0f, 0.0f, PlateCapNone));
        PlateCapCenters.Store(uint(targetPlate), float4(0.0f, 0.0f, 0.0f, PlateCapNone));
    }

    const OwnersData owners = LoadOwners(worldPointIndex);
    int bestPlate = owners.GetBestPlate();
    int bestCell = owners.GetBestCell();
    int secondPlate = owners.GetSecondPlate();
    int secondCell = owners.GetSecondCell();

    bool changed = false;

    const float3 worldPos = HealpixPixToVec(worldPointIndex, Params.HealpixOrder);
    const float3x4 targetRotation = LoadRotation(uint(targetPlate));
    const uint targetCell = HealpixVecToPix(WorldToPlate(worldPos, targetRotation), Params.HealpixOrder);
    const CrustFlagBits targetFlags = LoadCrustFlagsIn(uint(targetPlate) * Params.PointCount + targetCell);
    const bool targetAlive = targetFlags.IsAlive();
    const bool targetContinental = targetAlive && targetFlags.IsContinental();

    if (secondPlate == sourcePlate && secondCell >= 0)
    {
        const CrustFlagBits secondFlags = LoadCrustFlagsIn(uint(sourcePlate) * Params.PointCount + uint(secondCell));
        if (!secondFlags.IsAlive())
        {
            secondPlate = -1;
            secondCell = -1;
            changed = true;
        }
    }

    if (bestPlate == sourcePlate && bestCell >= 0)
    {
        const CrustFlagBits bestFlags = LoadCrustFlagsIn(uint(sourcePlate) * Params.PointCount + uint(bestCell));
        if (!bestFlags.IsAlive())
        {
            if (targetAlive)
            {
                if (secondPlate == targetPlate)
                {
                    secondPlate = -1;
                    secondCell = -1;
                }
                bestPlate = targetPlate;
                bestCell = int(targetCell);
            }
            else
            {
                bestPlate = secondPlate;
                bestCell = secondCell;
                secondPlate = -1;
                secondCell = -1;
            }
            changed = true;
        }
    }
    else if (bestPlate != targetPlate && targetContinental)
    {
        const bool bestIsContinental = (bestPlate >= 0 && bestCell >= 0)
            ? LoadCrustFlagsIn(uint(bestPlate) * Params.PointCount + uint(bestCell)).IsContinental()
            : false;
        if (!bestIsContinental)
        {
            secondPlate = bestPlate;
            secondCell = bestCell;
            bestPlate = targetPlate;
            bestCell = int(targetCell);
            changed = true;
        }
    }

    if (changed)
    {
        StoreOwners(worldPointIndex, int2(bestPlate, bestCell), int2(secondPlate, secondCell));
        if (bestPlate >= 0)
        {
            PreviousPlate.Store(worldPointIndex, bestPlate);
        }
    }
}
