module HashFill;
import LodestoneAttributes;

// The ComputeManifestTest cooks this module with Lodestone, runs it through rhi::ShaderLibrary, and compares
// every output word with the same hash computed on the CPU. The hash is integer arithmetic, so the
// comparison is exact: one wrong word anywhere in the path (cook, manifest, layout, dispatch) fails the test.

// a technique axis, so the test also proves that a query picks the right variant out of the manifest
[ls_axis_boolean][ls_axis_kind("technique")]
extern static const bool HASH_TWICE = false;

struct HashFillParams
{
    uint Seed;
    uint Count;
};

ConstantBuffer<HashFillParams> Params;
RWStructuredBuffer<uint> Output;

// PCG hash, from Jarzynski and Olano, "Hash Functions for GPU Rendering" (JCGT, 2020)
uint PcgHash(uint value)
{
    const uint state = value * 747796405u + 2891336453u;
    const uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void HashFillCS(uint3 threadId : SV_DispatchThreadID)
{
    if (threadId.x >= Params.Count)
    {
        return;
    }
    uint hashed = PcgHash(threadId.x ^ Params.Seed);
    if (HASH_TWICE)
    {
        hashed = PcgHash(hashed);
    }
    Output[threadId.x] = hashed;
}
