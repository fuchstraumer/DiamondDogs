implementing PlanetLab;
import LodestoneAttributes;
import LodestoneShims;

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

// The crust in the plate frames, [plate][cell] flat. Passes 1 and 2 read it, and pass 3 writes it.
FlagBytes CrustAliveBytes;
FlagBytes CrustContinentalBytes;
Words CrustAliveWords;
Words CrustContinentalWords;
Floats CrustBirth;

uint LoadAlive(uint flat)
{
    if (CRUST_FLAG_BYTES)
    {
        return uint(CrustAliveBytes.Load(flat));
    }

    return CrustAliveWords.Load(flat);
}

uint LoadContinental(uint flat)
{
    if (CRUST_FLAG_BYTES)
    {
        return uint(CrustContinentalBytes.Load(flat));
    }

    return CrustContinentalWords.Load(flat);
}

void StoreAlive(uint flat, uint value)
{
    if (CRUST_FLAG_BYTES)
    {
        CrustAliveBytes.Store(flat, uint8_t(value));
        return;
    }

    CrustAliveWords.Store(flat, value);
}

void StoreContinental(uint flat, uint value)
{
    if (CRUST_FLAG_BYTES)
    {
        CrustContinentalBytes.Store(flat, uint8_t(value));
        return;
    }

    CrustContinentalWords.Store(flat, value);
}