// Integer hashes for the noise library.
//
// pcg3d and pcg4d are from Mark Jarzynski and Marc Olano, "Hash Functions for GPU Rendering", Journal of Computer
// Graphics Techniques (JCGT), vol. 9, no. 3, 2020. They build on the PCG family of Melissa O'Neill. This file
// implements them from the paper's description. The multiply-shift range reduction in HashToRange is Daniel Lemire's
// ("Fast Random Integer Generation in an Interval", ACM TOMACS 2019), cut to 16 bits so that it stays in 32-bit integers.
module NoiseHash;

public uint3 Pcg3d(uint3 v)
{
    v = v * 1664525u + 1013904223u;

    v.x += v.y * v.z;
    v.y += v.z * v.x;
    v.z += v.x * v.y;

    v ^= v >> 16u;

    v.x += v.y * v.z;
    v.y += v.z * v.x;
    v.z += v.x * v.y;
    return v;
}

public uint4 Pcg4d(uint4 v)
{
    v = v * 1664525u + 1013904223u;

    v.x += v.y * v.w;
    v.y += v.z * v.x;
    v.z += v.x * v.y;
    v.w += v.y * v.z;

    v ^= v >> 16u;

    v.x += v.y * v.w;
    v.y += v.z * v.x;
    v.z += v.x * v.y;
    v.w += v.y * v.z;
    return v;
}

/// Returns four independent 32-bit hashes of one lattice cell. The seed is the fourth lane, so every bit of the seed
/// reaches every output bit.
public uint4 HashCell(int3 cell, uint seed)
{
    return Pcg4d(uint4(asuint(cell), seed));
}

/// Returns a value in [0, count) from the high 16 bits of a hash. The bias is at most count / 65536.
public uint HashToRange(uint hash, uint count)
{
    return ((hash >> 16u) * count) >> 16u;
}

/// Returns a value in [-1, 1] from all 32 bits of a hash.
public float HashToSignedUnit(uint hash)
{
    return float(hash) * (2.0 / 4294967295.0) - 1.0;
}
