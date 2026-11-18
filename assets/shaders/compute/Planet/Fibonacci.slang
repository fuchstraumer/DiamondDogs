// Spherical Fibonacci Mapping: Keinert, Innmann, Saenger, Stamminger (SIGGRAPH Asia 2015).
// Reference: planetlab/fibonacci.py Nearest32.
implementing PlanetLab;

internal static const float GoldenRatio = 1.61803398874989f;
internal static const float GoldenAngle = 2.399963229728653f;
internal static const float TwoPi = 6.283185307179586f;
internal static const float Sqrt5 = 2.23606797749979f;

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

internal int ReflectIndex(int index, int count)
{
    // past the north pole comes back as -i-1, past the south pole as 2N-i-1
    int reflected = index;
    if (reflected < 0)
    {
        reflected = -reflected - 1;
    }
    
    if (reflected >= count)
    {
        reflected = 2 * count - reflected - 1;
    }

    return clamp(reflected, 0, count - 1);
}

internal int FibonacciNearest(float3 direction, int count)
{
    const float n = float(count);
    const float invN = 1.0f / n;
    const float cos_theta = clamp(direction.z, -1.0f, 1.0f);

    float azimuth = -(atan2(direction.y, direction.x) - 0.5f * GoldenAngle);
    azimuth -= TwoPi * round(azimuth / TwoPi);

    const float sin_squared = max(1.0f - cos_theta * cos_theta, 1.0e-30f);
    const float zone = max(2.0f, floor(log(n * 3.14159265f * Sqrt5 * sin_squared) / log(GoldenRatio * GoldenRatio)));

    const float fibonacci = pow(GoldenRatio, zone) / Sqrt5;
    const int first = int(round(fibonacci));
    const int second = int(round(fibonacci * GoldenRatio));

    const float sign = 1.0f - 2.0f * fmod(zone, 2.0f);
    const float step = pow(GoldenRatio, -zone);
    const float b00 = -TwoPi * sign * step;
    const float b01 = TwoPi * sign * step / GoldenRatio;
    const float b10 = -2.0f * float(first) * invN;
    const float b11 = -2.0f * float(second) * invN;

    const float determinant = b00 * b11 - b01 * b10;
    const float determinantInv = 1.0f / determinant;
    const float height = cos_theta - (1.0f - 1.0f / n);
    const int corner_0 = int(floor((b11 * azimuth - b01 * height) * determinantInv));
    const int corner_1 = int(floor((-b10 * azimuth + b00 * height) * determinantInv));

    float best_distance = 3.402823466e+38f;
    int best_index = 0;

    [ForceUnroll]
    for (int candidate = 0; candidate < 4; ++candidate)
    {
        const int2 offset = int2(candidate & 1, candidate >> 1);
        const int raw_index = first * (corner_0 + offset.x) + second * (corner_1 + offset.y);
        const int index = ReflectIndex(raw_index, count);

        const float3 point = LatticePoint(index, count);
        const float3 delta = point - direction;
        const float distance = dot(delta, delta);

        if (distance < best_distance)
        {
            best_distance = distance;
            best_index = index;
        }
    }

    return best_index;
}