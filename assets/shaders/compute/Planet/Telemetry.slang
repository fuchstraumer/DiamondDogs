implementing PlanetLab;
__include Buffers;
__include HEALPix;

// Optional GPU telemetry passes and native 1D HEALPix state dump.
// Telemetry passes are registered in Loom only when PlanetGraphConfig::EnableTelemetry is true; when they are
// not registered they cost nothing, in every build configuration.
//
// Slot layout (160 uint words, mirrored 1:1 in PlanetGraph.hpp::telemetry_slots):
//   [0..31]   Cumulative event counters and 64-bit integer height sums in meters (order-independent)
//   [32..63]  Latest-step world-surface counters (computed over world points via OwnersIn)
//   [64..95]  Latest-step per-plate live cell counts (up to 32 plates)
//   [96..127] Latest-step per-plate continental cell counts (up to 32 plates)
//   [128..143] Latest-step world-surface continental relief histogram (16 bins, 0.5 km over [0, 8) km)
//   [144..159] Reserved

static const uint TelemetryCounterWords = 160u;

// Cumulative counters (zeroed on init/reset, accumulated across steps)
static const uint TelemetrySlotStepsObserved              = 0u;
static const uint TelemetrySlotCollisionsTriggered        = 1u;
static const uint TelemetrySlotTerraneCellsTransferred    = 2u;
static const uint TelemetrySlotOceanCellsBorn             = 3u;
static const uint TelemetrySlotSubductionOverlapPoints    = 4u;
static const uint TelemetrySlotUpliftOceanToContFlips     = 5u;
static const uint TelemetrySlotUpliftVolcanicToContFlips  = 6u;
static const uint TelemetrySlotUpliftVolcanicOverwritten  = 7u;
static const uint TelemetrySlotPlumeOceanToContFlips      = 8u;
static const uint TelemetrySlotPlumeVolcanicCellsActive   = 9u;
static const uint TelemetrySlotRiftsCompleted             = 10u;
static const uint TelemetrySlotRiftsBlockedNoFreeSlot     = 11u;
static const uint TelemetrySlotRiftsBlockedNotWarm        = 12u;
static const uint TelemetrySlotRiftsBlockedTooSmall       = 13u;
static const uint TelemetrySlotRiftsBlockedPickFailed     = 14u;
static const uint TelemetrySlotCollapseCellsActive        = 15u;
static const uint TelemetrySlotSnapOverflowCells          = 16u;

// Cumulative 64-bit integer height sums in meters (lo/hi pairs; quantized per lane before WaveActiveSum
// so results are strictly invariant to LiveCell atomic cursor order). Host multiplies by 0.001 * cellAreaKm2.
static const uint TelemetrySlotUpliftHeightAddedMetersLo       = 18u;
static const uint TelemetrySlotUpliftHeightAddedMetersHi       = 19u;
static const uint TelemetrySlotUpliftLinearErosionMetersLo     = 20u;
static const uint TelemetrySlotUpliftLinearErosionMetersHi     = 21u;
static const uint TelemetrySlotPlumeHeightAddedMetersLo        = 22u;
static const uint TelemetrySlotPlumeHeightAddedMetersHi        = 23u;
static const uint TelemetrySlotPlumeOceanicErosionMetersLo     = 24u;
static const uint TelemetrySlotPlumeOceanicErosionMetersHi     = 25u;
static const uint TelemetrySlotErosionDiffusionAddedMetersLo   = 26u;
static const uint TelemetrySlotErosionDiffusionAddedMetersHi   = 27u;
static const uint TelemetrySlotErosionRemovedMetersLo          = 28u;
static const uint TelemetrySlotErosionRemovedMetersHi          = 29u;

// Latest-step snapshot scalars (reset at the start of each step in TelemetryPreUpliftCS)
static const uint TelemetrySlotPlateFrameLiveCells        = 32u;
static const uint TelemetrySlotWorldCells                 = 33u;
static const uint TelemetrySlotContinentalCells           = 34u;
static const uint TelemetrySlotOceanicCells               = 35u;
static const uint TelemetrySlotVolcanicOceanicCells       = 36u;
static const uint TelemetrySlotAndeanCells                = 37u;
static const uint TelemetrySlotHimalayanCells             = 38u;
static const uint TelemetrySlotIslandArcCells             = 39u;
static const uint TelemetrySlotSubductionFrontCells       = 40u;
static const uint TelemetrySlotCollisionFrontCells        = 41u;
static const uint TelemetrySlotYoungOceanCellsUnder5My    = 42u;
static const uint TelemetrySlotOceanAgeSumTenthsMy        = 43u;
static const uint TelemetrySlotLandBelow05KmCells         = 44u;
static const uint TelemetrySlotLandAbove5KmCells          = 45u;
static const uint TelemetrySlotReliefMaxMm                = 46u;
static const uint TelemetrySlotLastStepCollisionFired     = 47u;
static const uint TelemetrySlotLastStepRiftOutcome        = 48u;

// Latest-step per-plate counts (up to 32 plates) and relief histogram
static const uint TelemetrySlotPlateCellCountBase         = 64u;
static const uint TelemetrySlotPlateContCountBase         = 96u;
static const uint TelemetrySlotReliefHistBase             = 128u; // 16 bins [0..8) km, 0.5 km width

internal void TelemetryWaveAdd(uint slot, uint value)
{
    const uint waveSum = WaveActiveSum(value);

    if (WaveIsFirstLane() && waveSum != 0u)
    {
        TelemetryCounters.Add(slot, waveSum);
    }
}

internal void TelemetryWaveAdd64(uint loSlot, uint hiSlot, uint laneValue)
{
    const uint waveSum = WaveActiveSum(laneValue);

    if (WaveIsFirstLane() && waveSum != 0u)
    {
        const uint prevLo = TelemetryCounters.Add(loSlot, waveSum);

        if (prevLo + waveSum < prevLo)
        {
            TelemetryCounters.Add(hiSlot, 1u);
        }
    }
}

internal uint PackTelemetryMeta(bool isContinental, int orogenyType)
{
    const uint contBit = isContinental ? 1u : 0u;
    const uint orogBits = (uint(clamp(orogenyType, 0, 15)) & 15u) << 1u;
    return contBit | orogBits;
}

internal void StoreTelemetrySnap(uint slot, bool isContinental, int orogenyType, float reliefKm)
{
    const uint base = slot * 2u;
    TelemetryCellSnap.Store(base + 0u, PackTelemetryMeta(isContinental, orogenyType));
    TelemetryCellSnap.Store(base + 1u, asuint(reliefKm));
}

internal void LoadTelemetrySnap(uint slot, out bool isContinental, out int orogenyType, out float reliefKm)
{
    const uint base = slot * 2u;
    const uint meta = TelemetryCellSnap.Load(base + 0u);
    isContinental = (meta & 1u) != 0u;
    orogenyType = int((meta >> 1u) & 15u);
    reliefKm = asfloat(TelemetryCellSnap.Load(base + 1u));
}

// Runs right before FrontDistanceAndUpliftCS: snapshots pre-uplift state per live cell, resets
// per-step snapshot counters, and records the collision ledger from Collide.slang.
[shader("compute")]
[numthreads(64, 1, 1)]
void TelemetryPreUpliftCS(uint3 threadId : SV_DispatchThreadID)
{
    const uint slot = threadId.x;
    const uint liveLimit = PlateLiveOffset.Load(StepParams.PlateCount);
    const uint snapCap = StepParams.PointCount * 2u;

    if (slot == 0u)
    {
        TelemetryCounters.Add(TelemetrySlotStepsObserved, 1u);

        for (uint clearSlot = 32u; clearSlot < 64u; ++clearSlot)
        {
            TelemetryCounters.Store(clearSlot, 0u);
        }

        for (uint clearBin = 0u; clearBin < 16u; ++clearBin)
        {
            TelemetryCounters.Store(TelemetrySlotReliefHistBase + clearBin, 0u);
        }

        TelemetryCounters.Store(TelemetrySlotPlateFrameLiveCells, liveLimit);
        TelemetryCounters.Store(TelemetrySlotWorldCells, StepParams.PointCount);

        if (liveLimit > snapCap)
        {
            TelemetryCounters.Add(TelemetrySlotSnapOverflowCells, liveLimit - snapCap);
        }

        const bool collisionFired = CollisionJobIn.Load(JobSlotPlateA) != NoCollisionPair;

        if (collisionFired)
        {
            TelemetryCounters.Add(TelemetrySlotCollisionsTriggered, 1u);
            TelemetryCounters.Add(TelemetrySlotTerraneCellsTransferred, CollideScratchCounters.Load(ScratchSlotReceivedCount));
            TelemetryCounters.Store(TelemetrySlotLastStepCollisionFired, 1u);
        }
    }

    if (slot >= liveLimit || slot >= snapCap)
    {
        return;
    }

    const uint flattened = LiveCell.Load(slot);
    const CrustFlagBits flags = LoadCrustFlagsIn(flattened);
    const CrustStateData state = LoadCrustStateIn(flattened);
    StoreTelemetrySnap(slot, flags.IsContinental(), state.GetOrogenyType(), state.GetRelief());
}

// Runs right after FrontDistanceAndUpliftCS: counts crust/provenance flips and uplift/erosion height changes.
[shader("compute")]
[numthreads(64, 1, 1)]
void TelemetryPostUpliftCS(uint3 threadId : SV_DispatchThreadID)
{
    const uint slot = threadId.x;
    const uint liveLimit = PlateLiveOffset.Load(StepParams.PlateCount);
    const uint snapCap = StepParams.PointCount * 2u;

    if (slot >= liveLimit || slot >= snapCap)
    {
        return;
    }

    bool oldCont;
    int oldOrog;
    float oldRelief;
    LoadTelemetrySnap(slot, oldCont, oldOrog, oldRelief);

    const uint flattened = LiveCell.Load(slot);
    const CrustFlagBits flags = LoadCrustFlagsIn(flattened);
    const CrustStateData state = LoadCrustStateIn(flattened);
    const bool newCont = flags.IsContinental();
    const int newOrog = state.GetOrogenyType();
    const float newRelief = state.GetRelief();

    const bool oceanToCont = !oldCont && newCont;
    const bool volcanicToCont = !oldCont && (oldOrog == OrogenyVolcanic) && newCont;
    const bool volcanicOverwritten = !oldCont && (oldOrog == OrogenyVolcanic) && (newOrog != OrogenyVolcanic);

    uint addedMeters = 0u;
    uint erodedMeters = 0u;

    if (!oceanToCont)
    {
        const float delta = newRelief - oldRelief;
        addedMeters = uint(round(max(delta, 0.0f) * 1000.0f));
        erodedMeters = uint(round(max(-delta, 0.0f) * 1000.0f));
    }

    TelemetryWaveAdd(TelemetrySlotUpliftOceanToContFlips, oceanToCont ? 1u : 0u);
    TelemetryWaveAdd(TelemetrySlotUpliftVolcanicToContFlips, volcanicToCont ? 1u : 0u);
    TelemetryWaveAdd(TelemetrySlotUpliftVolcanicOverwritten, volcanicOverwritten ? 1u : 0u);
    TelemetryWaveAdd64(TelemetrySlotUpliftHeightAddedMetersLo, TelemetrySlotUpliftHeightAddedMetersHi, addedMeters);
    TelemetryWaveAdd64(TelemetrySlotUpliftLinearErosionMetersLo, TelemetrySlotUpliftLinearErosionMetersHi, erodedMeters);

    StoreTelemetrySnap(slot, newCont, newOrog, newRelief);
}

// Runs right after ApplyMantlePlumesCS: counts plume accretion flips, active volcanic cells, and edifice budget.
[shader("compute")]
[numthreads(64, 1, 1)]
void TelemetryPostPlumesCS(uint3 threadId : SV_DispatchThreadID)
{
    const uint slot = threadId.x;
    const uint liveLimit = PlateLiveOffset.Load(StepParams.PlateCount);
    const uint snapCap = StepParams.PointCount * 2u;

    if (slot >= liveLimit || slot >= snapCap)
    {
        return;
    }

    bool oldCont;
    int oldOrog;
    float oldRelief;
    LoadTelemetrySnap(slot, oldCont, oldOrog, oldRelief);

    const uint flattened = LiveCell.Load(slot);
    const CrustFlagBits flags = LoadCrustFlagsIn(flattened);
    const CrustStateData state = LoadCrustStateIn(flattened);
    const bool newCont = flags.IsContinental();
    const int newOrog = state.GetOrogenyType();
    const float newRelief = state.GetRelief();

    const bool plumeAccreted = !oldCont && newCont;
    const float delta = newRelief - oldRelief;
    const uint addedMeters = uint(round(max(delta, 0.0f) * 1000.0f));
    const uint erodedMeters = uint(round(max(-delta, 0.0f) * 1000.0f));
    const bool activeVolcanic = (newOrog == OrogenyVolcanic) && (delta > 1.0e-5f);

    TelemetryWaveAdd(TelemetrySlotPlumeOceanToContFlips, plumeAccreted ? 1u : 0u);
    TelemetryWaveAdd(TelemetrySlotPlumeVolcanicCellsActive, activeVolcanic ? 1u : 0u);
    TelemetryWaveAdd64(TelemetrySlotPlumeHeightAddedMetersLo, TelemetrySlotPlumeHeightAddedMetersHi, addedMeters);
    TelemetryWaveAdd64(TelemetrySlotPlumeOceanicErosionMetersLo, TelemetrySlotPlumeOceanicErosionMetersHi, erodedMeters);

    StoreTelemetrySnap(slot, newCont, newOrog, newRelief);
}

// Runs right after ApplyErosionCS: counts order-independent erosion height budgets and newly born ocean cells.
[shader("compute")]
[numthreads(64, 1, 1)]
void TelemetryPostErosionCS(uint3 threadId : SV_DispatchThreadID)
{
    const uint slot = threadId.x;
    const uint liveLimit = PlateLiveOffset.Load(StepParams.PlateCount);
    const uint snapCap = StepParams.PointCount * 2u;

    if (slot >= liveLimit || slot >= snapCap)
    {
        return;
    }

    bool oldCont;
    int oldOrog;
    float oldRelief;
    LoadTelemetrySnap(slot, oldCont, oldOrog, oldRelief);

    const uint flattened = LiveCell.Load(slot);
    const CrustFlagBits flags = LoadCrustFlagsIn(flattened);
    const CrustStateData state = LoadCrustStateIn(flattened);
    const bool isContinental = flags.IsContinental();
    const float relief = state.GetRelief();
    const float timeMy = UpliftConstants.Time;

    const float delta = relief - oldRelief;
    const uint addedMeters = uint(round(max(delta, 0.0f) * 1000.0f));
    const uint removedMeters = uint(round(max(-delta, 0.0f) * 1000.0f));

    const float preElevKm = BaseCrustElevationKm(isContinental, state.GetBirth(), timeMy) + oldRelief;
    const bool collapseHit = (preElevKm > ShapingConstants.CollapseElevationKm) && (delta < -1.0e-5f);
    const bool oceanBornNow = !isContinental && (abs(state.GetBirth() - timeMy) <= 1.0e-3f);

    TelemetryWaveAdd(TelemetrySlotOceanCellsBorn, oceanBornNow ? 1u : 0u);
    TelemetryWaveAdd64(TelemetrySlotErosionDiffusionAddedMetersLo, TelemetrySlotErosionDiffusionAddedMetersHi, addedMeters);
    TelemetryWaveAdd64(TelemetrySlotErosionRemovedMetersLo, TelemetrySlotErosionRemovedMetersHi, removedMeters);
    TelemetryWaveAdd(TelemetrySlotCollapseCellsActive, collapseHit ? 1u : 0u);
}

// Runs right after RefreshOwnersAfterRiftCS and before MovePlatesAndClearCountersCS:
// records rift outcome, per-plate cell/continental counts, and world-surface sphere totals
// through OwnersIn so LandFraction, ocean age, and relief quantiles match DumpHealpixStateCS 1:1.
[shader("compute")]
[numthreads(64, 1, 1)]
void TelemetryPostRiftCS(uint3 threadId : SV_DispatchThreadID)
{
    const uint point = threadId.x;
    const uint pointCount = StepParams.PointCount;
    const uint plateCount = StepParams.PlateCount;

    if (point < 32u)
    {
        const uint cellCount = (point < plateCount) ? uint(max(LoadPlateCountersIn(point).w, 0)) : 0u;
        const uint contCount = (point < plateCount) ? PlateContinentalCount.Load(point) : 0u;
        TelemetryCounters.Store(TelemetrySlotPlateCellCountBase + point, cellCount);
        TelemetryCounters.Store(TelemetrySlotPlateContCountBase + point, contCount);
    }

    if (point == 0u)
    {
        const uint outcome = RiftJobIn.Load(RiftJobOutcome);
        TelemetryCounters.Store(TelemetrySlotLastStepRiftOutcome, outcome);

        if (outcome == RiftOutcomeCompleted)
        {
            TelemetryCounters.Add(TelemetrySlotRiftsCompleted, 1u);
        }
        else if (outcome == RiftOutcomeNoFreeSlot)
        {
            TelemetryCounters.Add(TelemetrySlotRiftsBlockedNoFreeSlot, 1u);
        }
        else if (outcome == RiftOutcomeNotWarm)
        {
            TelemetryCounters.Add(TelemetrySlotRiftsBlockedNotWarm, 1u);
        }
        else if (outcome == RiftOutcomeTooSmall)
        {
            TelemetryCounters.Add(TelemetrySlotRiftsBlockedTooSmall, 1u);
        }
        else if (outcome == RiftOutcomePickFailed)
        {
            TelemetryCounters.Add(TelemetrySlotRiftsBlockedPickFailed, 1u);
        }
    }

    if (point >= pointCount)
    {
        return;
    }

    const uint frontType = LoadFrontStateIn(point).GetSubduction();
    const uint overlapSubduction = (frontType == FrontTypeSubductionOverlap) ? 1u : 0u;
    const uint isSubductionFront = (frontType == FrontTypeSubductionOverlap || frontType == FrontTypeSubductionBorder) ? 1u : 0u;
    const uint isCollisionFront = (frontType == FrontTypeCollision) ? 1u : 0u;

    TelemetryWaveAdd(TelemetrySlotSubductionOverlapPoints, overlapSubduction);
    TelemetryWaveAdd(TelemetrySlotSubductionFrontCells, isSubductionFront);
    TelemetryWaveAdd(TelemetrySlotCollisionFrontCells, isCollisionFront);

    const OwnersData owners = LoadOwnersIn(point);
    const int plate = owners.GetBestPlate();
    const int cell = owners.GetBestCell();

    bool isContinental = false;
    int orogenyType = 0;
    float relief = 0.0f;
    float oceanAgeMy = 0.0f;
    float elevKm = -6.0f;
    bool owned = false;

    if (plate >= 0 && cell >= 0)
    {
        owned = true;
        const uint flat = uint(plate) * pointCount + uint(cell);
        const CrustFlagBits flags = LoadCrustFlagsIn(flat);
        const CrustStateData state = LoadCrustStateIn(flat);
        isContinental = flags.IsContinental();
        orogenyType = state.GetOrogenyType();
        relief = state.GetRelief();
        const float timeMy = UpliftConstants.Time;
        const float3 worldPoint = HealpixPixToVec(point, StepParams.HealpixOrder);
        const float swellKm = EvaluatePlumeSwellKm(worldPoint, timeMy);
        elevKm = TectonicCellElevationKm(flags, state, timeMy) + swellKm;

        if (!isContinental)
        {
            oceanAgeMy = max(timeMy - state.GetBirth(), 0.0f);
        }
    }

    const bool youngOcean = owned && !isContinental && (oceanAgeMy < 5.0f);
    const uint oceanAgeTenths = (owned && !isContinental) ? uint(round(oceanAgeMy * 10.0f)) : 0u;
    const bool landBelow05 = owned && isContinental && (elevKm <= 0.55f);
    const bool landAbove5 = owned && isContinental && (elevKm > 5.0f);

    TelemetryWaveAdd(TelemetrySlotContinentalCells, (owned && isContinental) ? 1u : 0u);
    TelemetryWaveAdd(TelemetrySlotOceanicCells, (owned && !isContinental) ? 1u : 0u);
    TelemetryWaveAdd(TelemetrySlotVolcanicOceanicCells, (owned && !isContinental && orogenyType == OrogenyVolcanic && relief > 0.05f) ? 1u : 0u);
    TelemetryWaveAdd(TelemetrySlotAndeanCells, (owned && orogenyType == OrogenyAndean) ? 1u : 0u);
    TelemetryWaveAdd(TelemetrySlotHimalayanCells, (owned && orogenyType == OrogenyHimalayan) ? 1u : 0u);
    TelemetryWaveAdd(TelemetrySlotIslandArcCells, (owned && orogenyType == OrogenyIslandArc) ? 1u : 0u);
    TelemetryWaveAdd(TelemetrySlotYoungOceanCellsUnder5My, youngOcean ? 1u : 0u);
    TelemetryWaveAdd(TelemetrySlotOceanAgeSumTenthsMy, oceanAgeTenths);
    TelemetryWaveAdd(TelemetrySlotLandBelow05KmCells, landBelow05 ? 1u : 0u);
    TelemetryWaveAdd(TelemetrySlotLandAbove5KmCells, landAbove5 ? 1u : 0u);

    const uint reliefMm = (owned && isContinental) ? uint(round(max(relief, 0.0f) * 1000.0f)) : 0u;
    const uint waveMaxMm = WaveActiveMax(reliefMm);

    if (WaveIsFirstLane() && waveMaxMm != 0u)
    {
        TelemetryCounters.Max(TelemetrySlotReliefMaxMm, waveMaxMm);
    }

    const uint histBin = min(uint(max(relief, 0.0f) * 2.0f), 15u);

    [unroll]
    for (uint bin = 0u; bin < 16u; ++bin)
    {
        TelemetryWaveAdd(TelemetrySlotReliefHistBase + bin, (owned && isContinental && histBin == bin) ? 1u : 0u);
    }
}

// On-demand native 1D equal-area HEALPix state packer (24 bytes per world cell, nested order, full f32 precision).
// Layout per cell (6 uint words):
//   word0 = owner_plate(u8, 255=none) | flags(u8)<<8 | orogeny_type(u8)<<16 | front_type(u8)<<24
//   word1 = asuint(elevation_km)   (base + relief + swell; matches mode 8 raw without shaping noise)
//   word2 = asuint(relief_km)
//   word3 = asuint(swell_km)
//   word4 = asuint(crust_age_my)   (time - birth, always crust age for both ocean and continent)
//   word5 = asuint(orogeny_age_my) (time - orogeny_time, always time since last orogeny)
[shader("compute")]
[numthreads(64, 1, 1)]
void DumpHealpixStateCS(uint3 threadId : SV_DispatchThreadID)
{
    const uint point = threadId.x;

    if (point >= StepParams.PointCount)
    {
        return;
    }

    const OwnersData owners = LoadOwnersIn(point);
    const int plate = owners.GetBestPlate();
    const int cell = owners.GetBestCell();
    const uint frontType = LoadFrontStateIn(point).GetSubduction();

    uint plateByte = 0xffu;
    uint flagByte = 0u;
    uint orogenyByte = 0u;
    float elevKm = -6.0f;
    float reliefKm = 0.0f;
    float swellKm = 0.0f;
    float crustAgeMy = 0.0f;
    float orogenyAgeMy = 0.0f;

    if (plate >= 0 && cell >= 0)
    {
        plateByte = uint(plate) & 0xffu;
        const uint flat = uint(plate) * StepParams.PointCount + uint(cell);
        const CrustFlagBits flags = LoadCrustFlagsIn(flat);
        const CrustStateData state = LoadCrustStateIn(flat);

        flagByte = flags.Bits & 0xffu;
        orogenyByte = uint(clamp(state.GetOrogenyType(), 0, 255)) & 0xffu;
        reliefKm = state.GetRelief();
        crustAgeMy = max(RenderMapConstants.Time - state.GetBirth(), 0.0f);
        orogenyAgeMy = max(RenderMapConstants.Time - state.GetOrogenyTime(), 0.0f);

        const float3 worldPoint = HealpixPixToVec(point, StepParams.HealpixOrder);
        swellKm = EvaluatePlumeSwellKm(worldPoint, RenderMapConstants.Time);
        elevKm = TectonicCellElevationKm(flags, state, RenderMapConstants.Time) + swellKm;
    }

    const uint word0 = plateByte | (flagByte << 8u) | (orogenyByte << 16u) | ((frontType & 0xffu) << 24u);
    const uint base = point * 6u;
    HealpixStateDump.Store(base + 0u, word0);
    HealpixStateDump.Store(base + 1u, asuint(elevKm));
    HealpixStateDump.Store(base + 2u, asuint(reliefKm));
    HealpixStateDump.Store(base + 3u, asuint(swellKm));
    HealpixStateDump.Store(base + 4u, asuint(crustAgeMy));
    HealpixStateDump.Store(base + 5u, asuint(orogenyAgeMy));
}
