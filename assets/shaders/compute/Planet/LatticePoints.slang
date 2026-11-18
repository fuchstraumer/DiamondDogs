implementing PlanetLab;
__include Buffers;
__include PlanetParams;

/*
    Previously, we used to compute the lattice points on the fly across all our shaders, but 
    that's redundant: we can precompute once when parameters change, and just read them in the
    rest of the shaders. This math is actually fairly complex and uses a number of transcendentals,
    so precomputation is actually likely worth here vs paying a memory read in the shaders
*/


// lets us avoid having to reference 64 bit uints at all: effectively `umulExtended`
internal void UMul32(in uint a, in uint b, out uint hi, out uint lo)
{
    const uint aLo = a & 0xFFFFu;
    const uint aHi = a >> 16;
    const uint bLo = b & 0xFFFFu;
    const uint bHi = b >> 16;

    const uint prodLoLo = aLo * bLo;
    const uint prodLoHi = aLo * bHi;
    const uint prodHiLo = aHi * bLo;
    const uint prodHiHi = aHi * bHi;

    const uint mid = (prodLoLo >> 16) + (prodLoHi & 0xFFFFu) + (prodHiLo & 0xFFFFu);
    lo = a * b;
    hi = prodHiHi + (prodLoHi >> 16) + (prodHiLo >> 16) + (mid >> 16);
}

/**
Returns lattice point `index` of `count` points: z = 1 - (2i + 1) / N, and an azimuth of (i + 0.5) * golden angle
in turns, computed with a wrapping 64-bit product so that float rounding does not grow with the index.
*/
internal float3 LatticePoint(int index, int count)
{
    const float invN = 1.0f / float(count);
    const float i = float(index);
    const float z = 1.0f - ((2.0f * i) + 1.0f) * invN;
    const float radius = sqrt(max(1.0f - z * z, 0.0f));

    // swapped to using explicit 32-bit multiplication and addition with carry to emulate a 64-bit product,
    // since I think using any 64 bit integral types breaks the WGSL compile
    const uint x = uint(index);
    const uint C_hi = 0x61C88646u;
    const uint C_lo = 0x80B583EAu;
    const uint O_hi = 0x30E44323u;
    const uint O_lo = 0x405AC1F5u;
    uint prodHigh = 0;
    uint prodLow = 0;
    UMul32(x, C_lo, prodHigh, prodLow);
    const uint low = prodLow + O_lo;
    const uint carry = (low < prodLow) ? 1u : 0u;
    const uint high = prodHigh + (x * C_hi) + O_hi + carry;
    const float turns = float(high >> 8) * (1.0f / 16777216.0f);

    const float azimuth = TwoPi * turns;

    float2 result;
    sincos(azimuth, result.y, result.x);
    float2 xyResult = radius.xx * result;
    return float3(xyResult, z);
}

[shader("compute")]
[numthreads(64, 1, 1)]
void ComputeLatticePointsCS(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint index = dispatchThreadID.x;
    if (index >= PlanetGenCellCount)
    {
        return;
    }

    // Compute the lattice point for this index
    const float3 point = LatticePoint(index, PlanetGenCellCount);
    LatticePoints.Store(index, float4(point, 0.0f));
}
