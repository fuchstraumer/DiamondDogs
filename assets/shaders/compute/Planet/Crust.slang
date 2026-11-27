implementing PlanetLab;

// Pass 3 of the plate port: new ocean crust at ridges, removal of crust that subducts, and the four front values
// that passes 4 to 6 read. Reference: tectonics.Step, first half. Two dispatches: the removal and the front values
// read the crust flags after the gap writes of the first dispatch. The crust flags and CrustBirth are the arrays
// that passes 1 and 2 read (Buffers.slang).

struct Pass3Params
{
    float Time;
};

ConstantBuffer<Pass3Params> Pass3Constants;

static const float PlanetRadiusKm = 6370.0f;
static const float MaximumPlateSpeed = 100.0f;
static const float MantleVigor = 1.0f;
static const float TrenchElevation = -10.0f;
static const float PeakElevation = 10.0f;
static const float ConvergentBoundarySpeedThreshold = 5.0f;

static const uint FrontTypeNone = 0u;
static const uint FrontTypeSubductionOverlap = 1u;
static const uint FrontTypeSubductionBorder = 2u;
static const uint FrontTypeCollision = 3u;

static const uint k_CardinalNeighborIndices[4] = { 0u, 2u, 4u, 6u };

float4 LoadPoleRates(uint plate);
void WriteNewOceanCrust(uint flat);
FrontState ComputeFrontValues(in CrustFlagBits crustFlagsUnder,
                              in uint point,
                              in float3 worldPoint,
                              in int topPlate,
                              in int underPlate,
                              in uint underFlat,
                              in uint subduction);

[shader("compute")]
[numthreads(64, 1, 1)]
void FillGapsCS(uint3 threadId : SV_DispatchThreadID)
{
    const uint point = threadId.x;
    if (point >= StepParams.PointCount)
    {
        return;
    }

    const OwnersData owners = LoadOwners(point);
    int plate = owners.GetBestPlate();
    int cell = owners.GetBestCell();

    if (plate == -1)
    {
        float3 worldPoint = HealpixPixToVec(point, StepParams.HealpixOrder);
        plate = PreviousPlate.Load(point);
        float3x4 rotation = LoadRotation(uint(plate));
        worldPoint = WorldToPlate(worldPoint, rotation);
        cell = int(HealpixVecToPix(worldPoint, StepParams.HealpixOrder));
        WriteNewOceanCrust(uint(plate) * StepParams.PointCount + uint(cell));
        StoreOwners(point, int2(plate, cell), owners.Data.zw);
    }

    PreviousPlate.Store(point, plate);
}

void RunSubductionFronts(uint point, bool killUnderCell);

// The reference port: each world point kills the lattice cell of its second plate, then writes the front values.
// The verify runs use it, because the frozen reference does the same. Lattices are rotated, so it misses the cells
// that no world point samples, and it can kill a cell that is the top cell at a nearby point.
[shader("compute")]
[numthreads(64, 1, 1)]
void RemoveSubductedCS(uint3 threadId : SV_DispatchThreadID)
{
    const uint point = threadId.x;
    if (point >= StepParams.PointCount)
    {
        return;
    }

    RunSubductionFronts(point, true);
}

// The same front values with no kill. KillSubductedCS does the kill after it, so no thread here reads a flag that
// another thread writes.
[shader("compute")]
[numthreads(64, 1, 1)]
void SubductionFrontsCS(uint3 threadId : SV_DispatchThreadID)
{
    const uint point = threadId.x;
    if (point >= StepParams.PointCount)
    {
        return;
    }

    RunSubductionFronts(point, false);
}

// True when Owners shows this cell on top at one of the 8 neighbours of worldPoint. Callers check worldPoint first.
// Owners maps a world point to a cell by rounding in the plate frame, and the kill maps a cell to a world point by
// rounding in the world frame. The two roundings differ near pixel edges, so a cell that is on top somewhere can map
// to a point where another plate is on top; any world point that maps to this cell lies within one pixel of worldPoint.
bool IsTopCellAtNeighbor(uint worldPoint, int plate, int cell)
{
    uint baseFace;
    int faceX;
    int faceY;
    HealpixDecode(worldPoint, StepParams.HealpixOrder, baseFace, faceX, faceY);

    for (uint direction = 0u; direction < 8u; ++direction)
    {
        const int neighbor = HealpixNeighbor(baseFace, faceX, faceY, k_NeighborDeltaX[direction],
                                             k_NeighborDeltaY[direction], StepParams.HealpixOrder);
        if (neighbor < 0)
        {
            continue;
        }

        if (all(LoadOwnersIn(uint(neighbor)).Data.xy == int2(plate, cell)))
        {
            return true;
        }
    }

    return false;
}

void KillIfSubducted(uint plate, uint cell);

// Removes subducted ocean crust by gather over the lattice cells of every plate, in the live-group layout of
// CountLiveCellsCS: a group holds one 32 x 32 tile of one plate, and skips the tile when it is outside the plate's
// cap. The only cells outside the cap are this step's FillGaps crust, which is on top and so never killed.
[shader("compute")]
[numthreads(256, 1, 1)]
void KillSubductedCS(uint3 groupID : SV_GroupID, uint3 laneInGroup : SV_GroupThreadID)
{
    const LiveGroup liveGroup = FindLiveGroup(groupID.x);
    if (laneInGroup.x == 0u)
    {
        GroupCulled = IsLiveGroupOutsideCap(liveGroup) ? 1u : 0u;
    }

    GroupMemoryBarrierWithGroupSync();

    if (GroupCulled != 0u)
    {
        return;
    }

    [unroll(CellsPerThread)]
    for (uint step = 0u; step < CellsPerThread; ++step)
    {
        const uint cell = liveGroup.FirstCell + step * LiveGroupSize + laneInGroup.x;
        if (cell >= StepParams.PointCount)
        {
            break;
        }

        KillIfSubducted(liveGroup.Plate, cell);
    }
}

// A live ocean cell that is not on top at any world point near it, and that lies under another plate, dies. Each
// thread reads only Owners and writes only its own cells, so the result does not depend on thread order. Continental
// cells do not subduct: collision handles them.
void KillIfSubducted(uint plate, uint cell)
{
    const uint flat = plate * StepParams.PointCount + cell;
    const CrustFlagBits flags = LoadCrustFlags(flat);
    if (!flags.IsAlive() || flags.IsContinental())
    {
        return;
    }

    const float3 localPos = HealpixPixToVec(cell, StepParams.HealpixOrder);
    const float3 worldPos = PlateToWorld(localPos, LoadRotation(plate));
    const uint worldPoint = HealpixVecToPix(worldPos, StepParams.HealpixOrder);
    const int topPlate = LoadOwnersIn(worldPoint).GetBestPlate();
    if (topPlate < 0 || topPlate == int(plate))
    {
        return;
    }

    if (!IsTopCellAtNeighbor(worldPoint, int(plate), int(cell)))
    {
        StoreCrustFlags(flat, PackCrustFlags(false, false));
    }
}

void RunSubductionFronts(uint point, bool killUnderCell)
{
    FrontState frontState = ZeroFrontState();

    const OwnersData owners = LoadOwnersIn(point);
    const int topPlate = owners.GetBestPlate();
    const int underPlate = owners.GetSecondPlate();
    if (underPlate >= 0)
    {
        const uint topFlat = uint(topPlate) * StepParams.PointCount + uint(owners.GetBestCell());
        const uint underFlat = uint(underPlate) * StepParams.PointCount + uint(owners.GetSecondCell());
        const CrustFlagBits crustFlagsTop = LoadCrustFlags(topFlat);
        const CrustFlagBits crustFlagsUnder = LoadCrustFlags(underFlat);
        const bool collision = crustFlagsTop.IsContinental() && crustFlagsUnder.IsContinental();
        const float3 worldPoint = HealpixPixToVec(point, StepParams.HealpixOrder);
        const uint frontType = collision ? FrontTypeCollision : FrontTypeSubductionOverlap;
        frontState = ComputeFrontValues(crustFlagsUnder,
                                        point,
                                        worldPoint,
                                        topPlate,
                                        underPlate,
                                        underFlat,
                                        frontType);

        if (killUnderCell && !collision)
        {
            StoreCrustFlags(underFlat, PackCrustFlags(false, crustFlagsUnder.IsContinental()));
        }
    }
    else if (topPlate >= 0)
    {
        // Continuous convergent-boundary detection via edge-sharing HEALPix neighbors so sub-cell
        // timesteps (v * dt < cellSpacing) maintain unbroken subduction trenches and colliding faults.
        uint baseFace;
        uint faceX;
        uint faceY;
        HealpixDecode(point, StepParams.HealpixOrder, baseFace, faceX, faceY);

        int bestNeighborPlate = -1;
        uint bestNeighborFlat = 0u;
        CrustFlagBits bestNeighborFlags = { 0u };
        uint bestFrontType = FrontTypeNone;
        float bestClosingSpeed = ConvergentBoundarySpeedThreshold;

        const uint topFlat = uint(topPlate) * StepParams.PointCount + uint(owners.GetBestCell());
        const float3 worldPoint = HealpixPixToVec(point, StepParams.HealpixOrder);
        bool loadedTopState = false;
        CrustFlagBits topFlags = { 0u };
        float topBirth = 0.0f;
        float3 topVelocity = float3(0.0f, 0.0f, 0.0f);

        [unroll]
        for (uint cardinal = 0u; cardinal < 4u; ++cardinal)
        {
            const uint dirIndex = k_CardinalNeighborIndices[cardinal];
            const int neighborPoint = HealpixNeighbor(baseFace, int(faceX), int(faceY),
                                                      k_NeighborDeltaX[dirIndex], k_NeighborDeltaY[dirIndex],
                                                      StepParams.HealpixOrder);
            if (neighborPoint < 0)
            {
                continue;
            }

            const OwnersData neighborOwners = LoadOwnersIn(uint(neighborPoint));
            const int neighborPlate = neighborOwners.GetBestPlate();
            if (neighborPlate < 0 || neighborPlate == topPlate)
            {
                continue;
            }

            if (!loadedTopState)
            {
                topFlags = LoadCrustFlags(topFlat);
                topBirth = LoadCrustStateIn(topFlat).GetBirth();
                const float4 poleRatesTop = LoadPoleRates(uint(topPlate));
                topVelocity = cross(poleRatesTop.xyz * poleRatesTop.w, worldPoint) * PlanetRadiusKm;
                loadedTopState = true;
            }

            const float4 poleRatesNeighbor = LoadPoleRates(uint(neighborPlate));
            const float3 neighborVelocity = cross(poleRatesNeighbor.xyz * poleRatesNeighbor.w, worldPoint) * PlanetRadiusKm;
            const float3 neighborPos = HealpixPixToVec(uint(neighborPoint), StepParams.HealpixOrder);
            const float3 edgeNormal = normalize(worldPoint - neighborPos);
            const float closingSpeed = dot(neighborVelocity - topVelocity, edgeNormal);
            if (closingSpeed <= bestClosingSpeed)
            {
                continue;
            }

            const uint neighborFlat = uint(neighborPlate) * StepParams.PointCount + uint(neighborOwners.GetBestCell());
            const CrustFlagBits neighborFlags = LoadCrustFlags(neighborFlat);

            uint candidateType = FrontTypeNone;
            if (topFlags.IsContinental() && neighborFlags.IsContinental())
            {
                candidateType = FrontTypeCollision;
            }
            else if (topFlags.IsContinental() && !neighborFlags.IsContinental())
            {
                candidateType = FrontTypeSubductionBorder;
            }
            else if (!topFlags.IsContinental() && !neighborFlags.IsContinental())
            {
                const float neighborBirth = LoadCrustStateIn(neighborFlat).GetBirth();
                if (topBirth > neighborBirth || (topBirth == neighborBirth && topPlate < neighborPlate))
                {
                    candidateType = FrontTypeSubductionBorder;
                }
            }

            if (candidateType != FrontTypeNone)
            {
                bestClosingSpeed = closingSpeed;
                bestNeighborPlate = neighborPlate;
                bestNeighborFlat = neighborFlat;
                bestNeighborFlags = neighborFlags;
                bestFrontType = candidateType;
            }
        }

        if (bestFrontType != FrontTypeNone)
        {
            frontState = ComputeFrontValues(bestNeighborFlags,
                                            point,
                                            worldPoint,
                                            topPlate,
                                            bestNeighborPlate,
                                            bestNeighborFlat,
                                            bestFrontType);
        }
    }

    StoreFrontState(point, frontState);
}

float4 LoadPoleRates(uint plate)
{
    return PoleRatesIn.Load(plate);
}

void WriteNewOceanCrust(uint flat)
{
    const uint newFlagBits = PackCrustFlags(true, false);
    StoreCrustFlags(flat, newFlagBits);
    StoreCrustState(flat, 0.0f, Pass3Constants.Time, Pass3Constants.Time, 0);
}

FrontState ComputeFrontValues(in CrustFlagBits crustFlagsUnder,
                              in uint point,
                              in float3 worldPoint,
                              in int topPlate,
                              in int underPlate,
                              in uint underFlat,
                              in uint subduction)
{
    const CrustStateData crustState = LoadCrustStateIn(underFlat);
    const float age = Pass3Constants.Time - crustState.GetBirth();
    float base = 0.5f;
    if (!crustFlagsUnder.IsContinental())
    {
        base = max(-(2.6f + 0.345f * sqrt(age)), -6.0f);
    }

    const float height = (base + crustState.GetRelief() - TrenchElevation) / (PeakElevation - TrenchElevation);
    const float transferBase = clamp(height, 0.0f, 1.0f);
    const float transfer = transferBase * transferBase;
    const float4 poleRatesTop = LoadPoleRates(uint(topPlate));
    const float4 poleRatesUnder = LoadPoleRates(uint(underPlate));
    const float3 spin = (poleRatesTop.xyz * poleRatesTop.w) - (poleRatesUnder.xyz * poleRatesUnder.w);
    const float speed = length(cross(spin, worldPoint)) * PlanetRadiusKm;
    const float strength = clamp(speed / MaximumPlateSpeed, 0.0f, MantleVigor) * transfer;
    return { float4(strength, speed, age, asfloat(subduction)) };
}
