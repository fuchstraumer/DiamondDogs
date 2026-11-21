module LoomHashFill;
import LodestoneAttributes;

// Loom's first GPU proof (spec step 5), and the shaders of loom_tests' runtime tests. HashFillCS hashes each index
// into Output. HashAgainCS reads that result and hashes it again into Output2, so the two passes form two levels with
// a read after a write between them: the barrier at the level boundary is what synchronization validation checks.
// The hash is integer arithmetic, so the comparison with the CPU is exact.
//
// Unlike ComputeManifestTest's HashFill, the seed and the count are entry point constants: Loom delivers them as push
// constants through Graph::Set.

// a technique axis, so the test also proves that a graph pin picks the right variant
[ls_axis_boolean][ls_axis_kind("technique")]
extern static const bool HASH_TWICE = false;

RWStructuredBuffer<uint> Output;
StructuredBuffer<uint> Hashed;
RWStructuredBuffer<uint> Output2;

// PCG hash, from Jarzynski and Olano, "Hash Functions for GPU Rendering" (JCGT, 2020)
uint PcgHash(uint value)
{
    const uint state = value * 747796405u + 2891336453u;
    const uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void HashFillCS(uint3 threadId : SV_DispatchThreadID, uniform uint seed, uniform uint count)
{
    if (threadId.x >= count)
    {
        return;
    }

    uint hashed = PcgHash(threadId.x ^ seed);

    if (HASH_TWICE)
    {
        hashed = PcgHash(hashed);
    }

    Output[threadId.x] = hashed;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void HashAgainCS(uint3 threadId : SV_DispatchThreadID, uniform uint count)
{
    if (threadId.x >= count)
    {
        return;
    }

    Output2[threadId.x] = PcgHash(Hashed[threadId.x]);
}
