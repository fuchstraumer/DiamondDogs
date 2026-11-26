implementing PlanetLab;
__include Buffers;
__include HEALPix;

// Plate rifting: a plate splits in two along an irregular line that cuts its continent, and the halves move apart.
// Reference: tectonics.Rift and tectonics.AddPlate (PlanetLab). The GPU cannot grow its plate arrays, so the new half
// takes a free slot: a plate with no live cells. The host reserves spare slots up to the 32-plate limit, and plates
// that lose all their crust free their slots again. With no free slot, no plate rifts.
//
// Three dispatches, after ApplyMantlePlumesCS and before MovePlatesAndClearCountersCS:
//   DecideRiftCS               one wave: incubation clocks, the rift choice, the new poles, rotations and rift push.
//   SplitPlateCS               indirect: moves the cells on one side of the cut line to the free slot.
//   RefreshOwnersAfterRiftCS   indirect: points that showed a moved cell now show the new plate.

static const float RiftIncubationTimeMy = 100.0f;   // the mantle under a supercontinent warms (Phillips and Coltice 2010)
static const float RiftWaitMy = 40.0f;              // mean time from a warm mantle to the rift
static const float RiftMinimumArea = 0.04f;         // a plate below this fraction of the sphere does not rift
static const float RiftWarp = 0.075f;               // how irregular the cut line is
static const float RiftSlowSpeedKmPerMy = 5.0f;     // speed of each half in the slow phase (Brune 2016)
// 0.5 * (1 - cos(1.42 * 0.4537)): the cap of a supercontinent, as a fraction of the sphere (tectonics.SupercontinentArea)
static const float SupercontinentArea = 0.10018f;
// a plate rifts only with continental crust, but that crust can be a few percent of its grid: 512 tries
static const uint RiftPickAttempts = 512u;
static const uint RiftGroupSize = 32u;

static const uint RiftJobActive = 0u;
static const uint RiftJobSource = 1u;
static const uint RiftJobTarget = 2u;
static const uint RiftJobSeed = 3u;
static const uint RiftJobFirst = 4u;   // 3 words: the first pick, a plate-frame unit vector as float bits
static const uint RiftJobSecond = 7u;  // 3 words: the second pick
static const uint NoPlate = 0xffffffffu;

// incubation time of each plate, in My of warm mantle
Floats PlateIncubation;
[ls_resource_tags("Aliased:PlateRiftIn")]
Points PlateRiftRW;
Words RiftJob;
[ls_resource_tags("Aliased:RiftJob")]
WordsIn RiftJobIn;
// 3 words: the group count of SplitPlateCS and RefreshOwnersAfterRiftCS (0 when no plate rifts)
Words RiftDispatch;

internal uint RiftHash(uint value)
{
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    value ^= value >> 16u;
    return value;
}

internal float RiftHash01(uint value)
{
    return float(RiftHash(value) & 0x00ffffffu) / 16777216.0f;
}

internal float3 RiftRandomUnit(uint seed)
{
    const float z = RiftHash01(seed) * 2.0f - 1.0f;
    const float angle = RiftHash01(seed ^ 0x68e31da4u) * 6.2831853f;
    const float radius = sqrt(max(1.0f - z * z, 0.0f));
    return float3(radius * cos(angle), radius * sin(angle), z);
}

// tectonics: crust.WaveNoise, a sum of 8 plane waves with random directions and frequencies in [3, 10]
internal float RiftWaveNoise(in float3 direction, uint seed)
{
    float total = 0.0f;

    for (uint wave = 0u; wave < 8u; ++wave)
    {
        const uint waveSeed = RiftHash(seed + wave * 0x9e3779b9u);
        const float frequency = lerp(3.0f, 10.0f, RiftHash01(waveSeed ^ 0x2545f491u));
        const float phase = RiftHash01(waveSeed ^ 0x165667b1u) * 6.2831853f;
        total += sin(dot(direction, RiftRandomUnit(waveSeed) * frequency) + phase);
    }

    // divide by sqrt(0.5 * waveCount), as WaveNoise does
    return total * 0.5f;
}

internal float3 LoadRiftVector(uint slot)
{
    return float3(asfloat(RiftJobIn.Load(slot)), asfloat(RiftJobIn.Load(slot + 1u)), asfloat(RiftJobIn.Load(slot + 2u)));
}

internal void StoreRiftVector(uint slot, float3 value)
{
    RiftJob.Store(slot, asuint(value.x));
    RiftJob.Store(slot + 1u, asuint(value.y));
    RiftJob.Store(slot + 2u, asuint(value.z));
}

internal float3x3 LoadRotationRW(uint plate)
{
    return float3x3(RotationsRW.Load(plate * 3u + 0u).xyz,
                    RotationsRW.Load(plate * 3u + 1u).xyz,
                    RotationsRW.Load(plate * 3u + 2u).xyz);
}

internal void ClearRiftJob()
{
    RiftJob.Store(RiftJobActive, 0u);
    RiftDispatch.Store(0u, 0u);
    RiftDispatch.Store(1u, 1u);
    RiftDispatch.Store(2u, 1u);
}

// one wave: lane i is plate i. PlateCount <= 32 (the same limit as OffsetFrontPointsCS)
[shader("compute")]
[numthreads(32, 1, 1)]
void DecideRiftCS(uint3 threadId : SV_DispatchThreadID)
{
    const uint plate = threadId.x;
    const uint plateCount = StepParams.PlateCount;
    const uint pointCount = StepParams.PointCount;
    const float dt = StepParams.TimeStepMy;
    const float timeMy = MovePlatesConstants.Time;
    const uint timeSeed = RiftHash(asuint(timeMy) ^ 0x5bd1e995u);
    const bool isPlate = plate < plateCount;

    int cellCount = 0;
    uint continentalCount = 0u;
    float4 rift = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float incubation = 0.0f;

    if (isPlate)
    {
        cellCount = LoadPlateCountersIn(plate).w;
        continentalCount = PlateContinentalCount.Load(plate);
        rift = PlateRiftRW.Load(plate);
        incubation = PlateIncubation.Load(plate);
    }

    // the mantle warms under continental crust: fast under a supercontinent, slowly under a small continent
    const float continentalShare = float(continentalCount) / float(pointCount);
    incubation += dt * MantleVigor * min(continentalShare / SupercontinentArea, 1.0f);

    // a plate from the start state has had no rift; give it a random head start, as tectonics.Start does
    const bool neverRifted = all(rift.xyz == 0.0f);
    const float headStart = neverRifted ? RiftHash01(plate * 0x27d4eb2du + 0x3c6ef372u) * RiftIncubationTimeMy : 0.0f;
    const bool warm = incubation + headStart >= RiftIncubationTimeMy;
    const bool large = float(cellCount) > RiftMinimumArea * float(pointCount);
    const bool trial = RiftHash01(timeSeed ^ (plate * 0x85ebca6bu)) < dt * MantleVigor / RiftWaitMy;
    const bool enabled = MovePlatesConstants.RiftEnabled != 0u;
    const bool eligible = enabled && isPlate && warm && large && continentalCount >= 2u && trial;
    const bool isFree = isPlate && cellCount == 0 && continentalCount == 0u;

    // one rift for each step. Rotate the lane order with the time, so that low plate ids get no preference
    const uint rotation = timeSeed & (RiftGroupSize - 1u);
    const uint sourceKey = WaveActiveMin(eligible ? ((plate + RiftGroupSize - rotation) & (RiftGroupSize - 1u)) : NoPlate);
    const uint targetPlate = WaveActiveMin(isFree ? plate : NoPlate);
    uint sourcePlate = NoPlate;

    if (sourceKey != NoPlate && targetPlate != NoPlate)
    {
        sourcePlate = (sourceKey + rotation) & (RiftGroupSize - 1u);
    }

    // lane 0 picks two points of the continent: the cut line passes between them
    uint picked = 0u;
    float3 first = float3(0.0f, 0.0f, 0.0f);
    float3 second = float3(0.0f, 0.0f, 0.0f);

    if (plate == 0u && sourcePlate != NoPlate)
    {
        // sample cell indices of the plate's own grid, not slots of the live list: the order of the live list comes
        // from atomic cursors and changes from run to run, and a run must repeat exactly
        for (uint attempt = 0u; attempt < RiftPickAttempts && picked < 2u; ++attempt)
        {
            const uint cell = RiftHash(timeSeed + attempt * 0x9e3779b9u + sourcePlate) % pointCount;
            const CrustFlagBits flags = LoadCrustFlagsIn(sourcePlate * pointCount + cell);

            if (!flags.IsAlive() || !flags.IsContinental())
            {
                continue;
            }

            const float3 direction = HealpixPixToVec(cell, StepParams.HealpixOrder);

            if (picked == 0u)
            {
                first = direction;
                picked = 1u;
            }
            else if (dot(first, direction) < 0.9999f)
            {
                second = direction;
                picked = 2u;
            }
        }
    }

    picked = WaveReadLaneFirst(picked);
    first = WaveReadLaneFirst(first);
    second = WaveReadLaneFirst(second);

    // in the world, rotation about first x second carries first toward second. The halves take opposite signs
    float3 axis = float3(0.0f, 0.0f, 0.0f);
    bool splits = false;

    if (picked == 2u)
    {
        const float3x3 sourceRotation = LoadRotationRW(sourcePlate);
        const float3 cross12 = cross(mul(sourceRotation, first), mul(sourceRotation, second));
        const float crossLength = length(cross12);

        if (crossLength > 1.0e-6f)
        {
            axis = cross12 / crossLength;
            splits = true;
        }
    }

    if (isPlate)
    {
        const bool reset = splits && (plate == sourcePlate || plate == targetPlate);
        PlateIncubation.Store(plate, reset ? 0.0f : incubation);
    }

    if (plate != 0u)
    {
        return;
    }

    if (!splits)
    {
        ClearRiftJob();
        return;
    }

    const float slowRate = RiftSlowSpeedKmPerMy * MantleVigor / PlanetRadiusKm;
    PoleRatesRW.Store(targetPlate, float4(axis, slowRate));
    PoleRatesRW.Store(sourcePlate, float4(-axis, slowRate));
    PlateRiftRW.Store(targetPlate, float4(axis, timeMy));
    PlateRiftRW.Store(sourcePlate, float4(-axis, timeMy));

    // the new half starts in the frame of the old plate, so each moved cell keeps its index
    RotationsRW.Store(targetPlate * 3u + 0u, RotationsRW.Load(sourcePlate * 3u + 0u));
    RotationsRW.Store(targetPlate * 3u + 1u, RotationsRW.Load(sourcePlate * 3u + 1u));
    RotationsRW.Store(targetPlate * 3u + 2u, RotationsRW.Load(sourcePlate * 3u + 2u));

    // both caps are stale: CountLiveCellsCS scans both plates in full next step
    PlateCapCenters.Store(sourcePlate, float4(0.0f, 0.0f, 0.0f, PlateCapNone));
    PlateCapCenters.Store(targetPlate, float4(0.0f, 0.0f, 0.0f, PlateCapNone));

    RiftJob.Store(RiftJobActive, 1u);
    RiftJob.Store(RiftJobSource, sourcePlate);
    RiftJob.Store(RiftJobTarget, targetPlate);
    RiftJob.Store(RiftJobSeed, RiftHash(timeSeed ^ 0x1b873593u));
    StoreRiftVector(RiftJobFirst, first);
    StoreRiftVector(RiftJobSecond, second);

    RiftDispatch.Store(0u, (pointCount + 63u) / 64u);
    RiftDispatch.Store(1u, 1u);
    RiftDispatch.Store(2u, 1u);
}

// one thread for each cell of the source plate
[shader("compute")]
[numthreads(64, 1, 1)]
void SplitPlateCS(uint3 threadId : SV_DispatchThreadID)
{
    const uint cell = threadId.x;
    const uint pointCount = StepParams.PointCount;

    if (cell >= pointCount || RiftJobIn.Load(RiftJobActive) == 0u)
    {
        return;
    }

    const uint sourcePlate = RiftJobIn.Load(RiftJobSource);
    const uint sourceFlat = sourcePlate * pointCount + cell;
    const CrustFlagBits flags = LoadCrustFlags(sourceFlat);

    if (!flags.IsAlive())
    {
        return;
    }

    const uint seed = RiftJobIn.Load(RiftJobSeed);
    const float3 direction = HealpixPixToVec(cell, StepParams.HealpixOrder);
    const float3 warp = float3(RiftWaveNoise(direction, seed),
                               RiftWaveNoise(direction, seed ^ 0xa511e9b3u),
                               RiftWaveNoise(direction, seed ^ 0x63d83595u));
    const float3 warped = normalize(direction + RiftWarp * warp);
    const bool moves = dot(warped, LoadRiftVector(RiftJobSecond)) > dot(warped, LoadRiftVector(RiftJobFirst));

    if (!moves)
    {
        return;
    }

    const uint targetFlat = RiftJobIn.Load(RiftJobTarget) * pointCount + cell;
    StoreCrustFlags(targetFlat, flags.Bits);
    StoreCrustState(targetFlat, LoadCrustState(sourceFlat));
    StoreCrustFlags(sourceFlat, 0u);
}

// one thread for each world point. Both plates have the same rotation, so a moved cell keeps its index
[shader("compute")]
[numthreads(64, 1, 1)]
void RefreshOwnersAfterRiftCS(uint3 threadId : SV_DispatchThreadID)
{
    const uint point = threadId.x;
    const uint pointCount = StepParams.PointCount;

    if (point >= pointCount || RiftJobIn.Load(RiftJobActive) == 0u)
    {
        return;
    }

    const int sourcePlate = int(RiftJobIn.Load(RiftJobSource));
    const int targetPlate = int(RiftJobIn.Load(RiftJobTarget));
    const OwnersData owners = LoadOwners(point);
    int bestPlate = owners.GetBestPlate();
    int secondPlate = owners.GetSecondPlate();
    const int bestCell = owners.GetBestCell();
    const int secondCell = owners.GetSecondCell();

    if (bestPlate == sourcePlate && bestCell >= 0 &&
        LoadCrustFlagsIn(uint(targetPlate) * pointCount + uint(bestCell)).IsAlive())
    {
        bestPlate = targetPlate;
    }

    if (secondPlate == sourcePlate && secondCell >= 0 &&
        LoadCrustFlagsIn(uint(targetPlate) * pointCount + uint(secondCell)).IsAlive())
    {
        secondPlate = targetPlate;
    }

    if (bestPlate == owners.GetBestPlate() && secondPlate == owners.GetSecondPlate())
    {
        return;
    }

    StoreOwners(point, int2(bestPlate, bestCell), int2(secondPlate, secondCell));

    if (PreviousPlate.Load(point) == sourcePlate)
    {
        PreviousPlate.Store(point, bestPlate);
    }
}
