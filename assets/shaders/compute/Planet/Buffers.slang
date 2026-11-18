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
