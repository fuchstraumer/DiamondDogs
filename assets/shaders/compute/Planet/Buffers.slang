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

extern struct FloatsIn : IReadBuffer<float>;
extern struct IntsIn : IReadBuffer<int>;
extern struct Floats : IReadWriteBuffer<float>;
extern struct Ints : IReadWriteBuffer<int>;
extern struct Words : IReadWriteBuffer<uint>;
extern struct FlagBytes : IReadWriteBuffer<uint8_t>;
extern struct Counters : IAtomicBuffer<uint>;
// signed counters used during TorqueSum
extern struct SignedCounters : IAtomicBuffer<int>;
extern struct Points : IReadWriteBuffer<float4>;
extern struct PointsIn : IReadBuffer<float4>;

// The crust in the plate frames, [plate][cell] flat. Passes 1 and 2 read it, and pass 3 writes it.
FlagBytes CrustAliveBytes;
FlagBytes CrustContinentalBytes;
Words CrustAliveWords;
Words CrustContinentalWords;
Floats CrustBirth;
Points LatticePoints;
Counters PlateLiveCursor;
Words PlateLiveOffset;
Words LiveCell;
Words LiveDispatch;
SignedCounters PlatePositionSum;
SignedCounters PlatePull;
Counters PlateCellCount;
Counters PlateContinentalCount;
// stores per-plate rotations as float4x3s, so we only need 3 loads
PointsIn Rotations;

// `PlateCapCenter` stores the central "cap" of each plate around its live cells, in the plates frame.
// LiveCells.slang writes it, and then OwnersCS of the next tick reads it - so the first pass of OwnersCS
// will be *much* slower before this exists, but once it does successive ticks should be much faster
Points PlateCapCenters;
// 1-cos(cap radius) as float bits: it's never negative, so an atomic max on the uint bits is safely
// the same as atomic max on a float (which we can't really do in shaders at all)
Counters PlateCapSpread;

internal static const float PlateCapNone = 0.0f;
internal static const float PlateCapValid = 1.0f;
internal static const float PlateCapEmpty = 2.0f;

internal uint LoadAlive(uint flat)
{
    if (CRUST_FLAG_BYTES)
    {
        return uint(CrustAliveBytes.Load(flat));
    }

    return CrustAliveWords.Load(flat);
}

internal uint LoadContinental(uint flat)
{
    if (CRUST_FLAG_BYTES)
    {
        return uint(CrustContinentalBytes.Load(flat));
    }

    return CrustContinentalWords.Load(flat);
}

internal void StoreAlive(uint flat, uint value)
{
    if (CRUST_FLAG_BYTES)
    {
        CrustAliveBytes.Store(flat, uint8_t(value));
        return;
    }

    CrustAliveWords.Store(flat, value);
}

internal void StoreContinental(uint flat, uint value)
{
    if (CRUST_FLAG_BYTES)
    {
        CrustContinentalBytes.Store(flat, uint8_t(value));
        return;
    }

    CrustContinentalWords.Store(flat, value);
}

internal void AddPlatePositionSum(uint where, int3 position)
{
    PlatePositionSum.Add(where, position.x);
    PlatePositionSum.Add(where + 1u, position.y);
    PlatePositionSum.Add(where + 2u, position.z);
}

// todo: Can we coalesce this load somehow, but keep things atomic?
internal int3 LoadPlatePositionSum(uint where)
{
    int3 position;
    position.x = PlatePositionSum.Load(where);
    position.y = PlatePositionSum.Load(where + 1u);
    position.z = PlatePositionSum.Load(where + 2u);
    return position;
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
    return worldPos.x * rotation[0].xyz +
           worldPos.y * rotation[1].xyz +
           worldPos.z * rotation[2].xyz;
}

internal float3 PlateToWorld(in float3 platePos, in float3x4 rotation)
{
    return float3(dot(rotation[0].xyz, platePos),
                  dot(rotation[1].xyz, platePos),
                  dot(rotation[2].xyz, platePos));
}
