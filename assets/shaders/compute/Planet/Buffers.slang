implementing PlanetLab;
import LodestoneAttributes;
import LodestoneShims;
//__include PlanetParams;

// The buffer forms of the plate passes. Each buffer is a Lodestone shim, so a spirv cook binds it as a descriptor and a
// spirv-modern cook stores its device address, with no change here.

// The crust flags (alive, continental) take one byte for each cell when the device has 8-bit storage, and one word for
// each cell when it does not. A byte store changes only its own byte, so two threads that write neighbouring cells need
// no atomic in either form. The word form uses four times the memory (and bandwidth, more importantly)
[ls_axis_boolean][ls_axis_kind("Capability")]
extern static const bool CRUST_FLAG_BYTES = true;

extern struct Floats : IReadWriteBuffer<float>;
extern struct FloatsIn : IReadBuffer<float>;
extern struct Ints : IReadWriteBuffer<int>;
extern struct IntsIn : IReadBuffer<int>;
extern struct Words : IReadWriteBuffer<uint>;
extern struct WordsIn : IReadBuffer<uint>;
extern struct FlagBytes : IReadWriteBuffer<uint8_t>;
extern struct FlagBytesIn : IReadBuffer<uint8_t>;
extern struct Counters : IAtomicBuffer<uint>;
// signed counters used during TorqueSum
extern struct SignedCounters : IAtomicBuffer<int>;
extern struct Points : IReadWriteBuffer<float4>;
extern struct PointsIn : IReadBuffer<float4>;
extern struct IntPoints : IReadWriteBuffer<int4>;
extern struct IntPointsIn : IReadBuffer<int4>;
// only two passes use this, and they can get by with just two packed points
extern struct CellPoints : IReadWriteBuffer<int2>;

// stores alive and continental bits: bit 0 = alive, bit 1 = continental
FlagBytes CrustFlags;
[ls_resource_tags("Aliased:CrustFlags")]
FlagBytesIn CrustFlagsIn;
[ls_resource_tags("Aliased:CrustFlags")]
WordsIn CrustFlagsPackedIn;

Words CrustFlagsWords;
[ls_resource_tags("Aliased:CrustFlagsWords")]
WordsIn CrustFlagsWordsIn;

Points LatticePoints;
Counters PlateLiveCursor;
Words PlateLiveOffset;
Words LiveCell;
Words LiveDispatch;
Words LiveGroupCounts;
// PlateCounters stores per-plate counters into what is effectively an int4 array, indexed
// as scalar only because of the atomics required. xyz = PlatePositionSum, w = PlateCellCount
SignedCounters PlateCounters;
[ls_resource_tags("Aliased:PlateCounters")]
IntPointsIn PlateCountersIn;
SignedCounters PlatePull;
Counters PlateContinentalCount;
// stores per-plate rotations as float4x3s, so we only need 3 loads
PointsIn Rotations;
[ls_resource_tags("Aliased:Rotations")]
Points RotationsRW; // written by MovePlatesAndClearCountersCS

// `PlateCapCenter` stores the central "cap" of each plate around its live cells, in the plates frame.
// LiveCells.slang writes it, and then OwnersCS of the next tick reads it - so the first pass of OwnersCS
// will be *much* slower before this exists, but once it does successive ticks should be much faster
Points PlateCapCenters;
// 1-cos(cap radius) as float bits: it's never negative, so an atomic max on the uint bits is safely
// the same as atomic max on a float (which we can't really do in shaders at all)
Counters PlateCapSpread;

// this tag tells us to make this an alis of PlateCapSpread - but without the atomic instructions to read
// from it, since the Owners.slang is just trying to *only* read without needing atomicity
[ls_resource_tags("Aliased:PlateCapSpread")]
WordsIn PlateCapSpreadIn; // read by OwnersCS
[ls_resource_tags("Aliased:PlateFrontPointCount")]
WordsIn PlateFrontPointCountIn; // read by OffsetFrontPointsCS
[ls_resource_tags("Aliased:PairVotes")]
WordsIn PairVotesIn; // read by SwapToMajorityCS
[ls_resource_tags("Aliased:LowVotes")]
WordsIn LowVotesIn; // read by SwapToMajorityCS

// Used by Owners.slang (readonly via PreviousPlateIn) and FillGapsCS (updated in-place for next tick)
Ints PreviousPlate;
[ls_resource_tags("Aliased:PreviousPlate")]
IntsIn PreviousPlateIn;

// .x = BestPlate, .y = BestCell, .z = SecondPlate, .w = SecondCell
IntPoints Owners;
[ls_resource_tags("Aliased:Owners")]
IntPointsIn OwnersIn;

// Used by Front.slang
// P * P integer counts, indexed by low * PlateCount + high. The host clears both to zero before the first dispatch.
// Each vote is one plain atomic add, not AtomicAddWaveAggregated: the pair changes from lane to lane, and about one
// lane in a wave votes (808 of 100 000 points in the reference capture).
Counters PairVotes;
Counters LowVotes;

// Used by Crust.slang entrypoints (FillGapsCS, RemoveSubductedCS, and related functions)
PointsIn PoleRatesIn; // readonly view for Crust.slang
[ls_resource_tags("Aliased:PoleRatesIn")]
Points PoleRatesRW; // written by MovePlatesAndClearCountersCS
// per-plate rift push state: .xyz = rift_axis (zero if inactive), .w = rift_time (My)
PointsIn PlateRiftIn;
Points CrustState;
[ls_resource_tags("Aliased:CrustState")]
PointsIn CrustStateIn;

Points FrontData;
[ls_resource_tags("Aliased:FrontData")]
PointsIn FrontDataIn;

// Used by FrontPoints.slang and Uplift.slang
Counters PlateFrontPointCount;
Counters PlateFrontPointCursor;
Words PlateFrontPointOffset;
[ls_resource_tags("Aliased:PlateFrontPointOffset")]
WordsIn PlateFrontPointOffsetIn; // read by SortFrontPointsCS


CellPoints CellFronts;
Points FrontPoints;
Points FrontTileCaps;
Words FrontTileDispatch;

// Used by Collide.slang (Phase 2: Pure-GPU Continental Collision & Allegretti 2x2 BKE CCL)
// Per-pair P * P tables indexed by lowPlate * PlateCount + highPlate, cleared each tick.
Counters CollisionPairCount;
Counters CollisionPairSpeedSum;
Counters CollisionPairDeepCount;
[ls_resource_tags("Aliased:CollisionPairCount")]
WordsIn CollisionPairCountIn;
[ls_resource_tags("Aliased:CollisionPairSpeedSum")]
WordsIn CollisionPairSpeedSumIn;
[ls_resource_tags("Aliased:CollisionPairDeepCount")]
WordsIn CollisionPairDeepCountIn;

// 16-word selected collision job descriptor (see Collide.slang JobSlot* constants).
Words CollisionJob;
[ls_resource_tags("Aliased:CollisionJob")]
WordsIn CollisionJobIn;

// 12-word indirect dispatch table: 4 x {groupCountX, 1, 1} at byte offsets 0, 12, 24, 36.
// [0..2]  = 16x16 tile passes (2 * NumTiles_16x16)
// [3..5]  = 2x2 block passes  (ceil(2 * (PointCount / 4) / 64))
// [6..8]  = cell passes       (ceil(PointCount / 64))
// [9..11] = single-group passes (1)
Words CollideDispatch;

// Allegretti 2x2 BKE CCL buffers (2 * (PointCount / 4) = PointCount / 2 entries across PlateA & PlateB).
Counters BkeParent;
Words BkeMask;
Words BkeSeedFlag;

// Per-collision counters, suture anchors, geodesic suture distance, and 64-bin escape histogram.
Counters CollideScratchCounters;
SignedCounters SuturePositionSum;
Points SuturePoints;
Floats SutureDistance;
Counters EscapeHistogram;

// Strip-able GPU telemetry and native 1D HEALPix state dump (Telemetry.slang).
// Bound only when PlanetGraphConfig::EnableTelemetry (or EnableStateDump for the dump) is true.
Counters TelemetryCounters;
Words TelemetryCellSnap;
Words HealpixStateDump;

internal static const float PlateCapNone = 0.0f;
internal static const float PlateCapValid = 1.0f;
internal static const float PlateCapEmpty = 2.0f;
internal static const uint CrustFlagAlive = 1u << 0u;
internal static const uint CrustFlagContinental = 1u << 1u;
internal static const int OrogenyVolcanic = 4;

internal struct CrustFlagBits
{
    uint Bits;
    bool IsAlive()
    {
        return (Bits & CrustFlagAlive) != 0u;
    }
    bool IsContinental()
    {
        return (Bits & CrustFlagContinental) != 0u;
    }
}

internal CrustFlagBits LoadCrustFlags(uint where)
{
    // using an explicit branch because this is a compile-time static branch
    if (CRUST_FLAG_BYTES)
    {
        return { uint(CrustFlags.Load(where)) };
    }
    else
    {
        return { CrustFlagsWords.Load(where) };
    }
}

internal CrustFlagBits LoadCrustFlagsIn(uint where)
{
    if (CRUST_FLAG_BYTES)
    {
        return { uint(CrustFlagsIn.Load(where)) };
    }
    else
    {
        return { CrustFlagsWordsIn.Load(where) };
    }
}

internal uint PackCrustFlags(bool alive, bool continental)
{
    return (alive ? CrustFlagAlive : 0u) | (continental ? CrustFlagContinental : 0u);
}

internal void StoreCrustFlags(uint where, uint bits)
{
    if (CRUST_FLAG_BYTES)
    {
        CrustFlags.Store(where, uint8_t(bits));
    }
    else
    {
        CrustFlagsWords.Store(where, bits);
    }
}

internal void AddPlateCounters(uint where, int4 counters)
{
    PlateCounters.Add(where + 0u, counters.x);
    PlateCounters.Add(where + 1u, counters.y);
    PlateCounters.Add(where + 2u, counters.z);
    PlateCounters.Add(where + 3u, counters.w);
}

internal int4 LoadPlateCountersIn(uint where)
{
    return PlateCountersIn.Load(where);
}

internal void AddPlatePull(uint where, int3 pull)
{
    PlatePull.Add(where, pull.x);
    PlatePull.Add(where + 1u, pull.y);
    PlatePull.Add(where + 2u, pull.z);
}

internal int3 LoadPlatePull(uint where)
{
    int3 pull;
    pull.x = PlatePull.Load(where);
    pull.y = PlatePull.Load(where + 1u);
    pull.z = PlatePull.Load(where + 2u);
    return pull;
}

internal float3x4 LoadRotation(uint plate)
{
    float3x4 rotation;
    rotation[0] = Rotations.Load(plate * 3u + 0u);
    rotation[1] = Rotations.Load(plate * 3u + 1u);
    rotation[2] = Rotations.Load(plate * 3u + 2u);
    return rotation;
}

internal float3 WorldToPlate(in float3 worldPos, in float3x4 rotation)
{
    const float3x3 mat = float3x3(rotation[0].xyz, rotation[1].xyz, rotation[2].xyz);
    return mul(worldPos, mat);
}

internal float3 PlateToWorld(in float3 platePos, in float3x4 rotation)
{
    const float3x3 mat = float3x3(rotation[0].xyz, rotation[1].xyz, rotation[2].xyz);
    return mul(mat, platePos);
}

struct OwnersData
{
    int4 Data;

    int GetBestPlate()
    {
        return Data.x;
    }

    int GetBestCell()
    {
        return Data.y;
    }

    int GetSecondPlate()
    {
        return Data.z;
    }

    int GetSecondCell()
    {
        return Data.w;
    }

};

internal OwnersData LoadOwners(uint where)
{
    return { Owners.Load(where) };
}

internal OwnersData LoadOwnersIn(uint where)
{
    return { OwnersIn.Load(where) };
}

internal void StoreOwners(uint where, int2 best, int2 second)
{
    Owners.Store(where, int4(best.xy, second.xy));
}

internal void StoreOwners(uint where, OwnersData owners)
{
    Owners.Store(where, owners.Data);
}

// CrustState gets an actual struct - it's more complex, and it would be too easy for users to mix up
// the members without direct getters/setters. It's also one of the ones with more tangled read/write
// access, or just singular reads: most of the rest of our coalesced types always work in batches
struct CrustStateData
{
    float4 Data;

    float GetRelief()
    {
        return Data.x;
    }

    [mutating]
    void SetRelief(in float relief)
    {
        Data.x = relief;
    }

    float GetBirth()
    {
        return Data.y;
    }

    [mutating]
    void SetBirth(in float birth)
    {
        Data.y = birth;
    }

    float GetOrogenyTime()
    {
        return Data.z;
    }

    [mutating]
    void SetOrogenyTime(in float orogenyTime)
    {
        Data.z = orogenyTime;
    }

    int GetOrogenyType()
    {
        return asint(Data.w);
    }

    [mutating]
    void SetOrogenyType(in int orogenyType)
    {
        Data.w = asfloat(orogenyType);
    }
};

internal CrustStateData ZeroCrustState()
{
    return { float4(0, 0, 0, 0) };
}

internal CrustStateData LoadCrustState(uint where)
{
    return { CrustState.Load(where) };
}

internal CrustStateData LoadCrustStateIn(uint where)
{
    return { CrustStateIn.Load(where) };
}

internal void StoreCrustState(uint where, float relief, float birth, float orogenyTime, int orogenyType)
{
    CrustState.Store(where, float4(relief, birth, orogenyTime, asfloat(orogenyType)));
}

internal void StoreCrustState(uint where, CrustStateData crustState)
{
    CrustState.Store(where, crustState.Data);
}

internal float BaseCrustElevationKm(in bool isContinental, in float birthTimeMy, in float timeMy)
{
    if (isContinental)
    {
        return 0.5f;
    }

    const float oceanAgeMy = max(timeMy - birthTimeMy, 0.0f);
    return max(-(2.6f + 0.345f * sqrt(oceanAgeMy)), -6.0f);
}

internal float TectonicCellElevationKm(in CrustFlagBits flags, in CrustStateData state, in float timeMy)
{
    return BaseCrustElevationKm(flags.IsContinental(), state.GetBirth(), timeMy) + state.GetRelief();
}

struct FrontState
{
    float4 Data;

    float GetStrength()
    {
        return Data.x;
    }

    float GetSpeed()
    {
        return Data.y;
    }

    float GetSlabAge()
    {
        return Data.z;
    }

    uint GetSubduction()
    {
        return asuint(Data.w);
    }

    [mutating]
    void SetStrength(in float strength)
    {
        Data.x = strength;
    }

    [mutating]
    void SetSpeed(in float speed)
    {
        Data.y = speed;
    }

    [mutating]
    void SetSlabAge(in float slabAge)
    {
        Data.z = slabAge;
    }

    [mutating]
    void SetSubduction(in uint subduction)
    {
        Data.w = asfloat(subduction);
    }

};

internal FrontState LoadFrontState(uint where)
{
    return { FrontData.Load(where) };
}

internal FrontState LoadFrontStateIn(uint where)
{
    return { FrontDataIn.Load(where) };
}

internal void StoreFrontState(uint where, FrontState frontState)
{
    FrontData.Store(where, frontState.Data);
}

internal FrontState ZeroFrontState()
{
    return { float4(0, 0, 0, 0) };
}

struct CellState
{
    int2 Data;

    int GetFrontPoint()
    {
        return Data.x;
    }

    float GetDistance()
    {
        return asfloat(Data.y);
    }
};

internal CellState LoadCellState(uint where)
{
    return { CellFronts.Load(where) };
}

internal void StoreCellState(uint where, int frontPoint, float distance)
{
    CellFronts.Store(where, int2(frontPoint, asint(distance)));
}
