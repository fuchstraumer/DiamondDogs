module LoomSizeTest;
import LodestoneAttributes;

// Loom's Size tests, and Bind's view rule (LM307). Counters is an atomic buffer of 64 uints; CountersIn is a read-only
// view of the same 256 bytes as 16 uint4s, so the two strides differ and the byte sizes agree. Fixed states 32 floats
// (128 bytes), which no 256-byte resource can match. Values states no size, so the graph must give it one. Args
// carries indirect dispatch arguments.

[ls_element_count("64")]
RWStructuredBuffer<uint> Counters;

[ls_element_count("16")]
[ls_resource_tags("ViewOf:Counters")]
StructuredBuffer<uint4> CountersIn;

[ls_element_count("32")]
StructuredBuffer<float> Fixed;

RWStructuredBuffer<float> Values;

[ls_element_count("3")]
[ls_resource_role("IndirectDispatch")]
RWStructuredBuffer<uint> Args;

// Both views in one pass: LM307 checks that they name one key.
[shader("compute")]
[numthreads(64, 1, 1)]
void CountCS(uint3 threadId : SV_DispatchThreadID)
{
    uint previous;
    InterlockedAdd(Counters[threadId.x & 63u], 1u, previous);
    Values[threadId.x] = float(previous + CountersIn[threadId.x & 15u].x);
}

// The read-only view alone.
[shader("compute")]
[numthreads(64, 1, 1)]
void ReadCS(uint3 threadId : SV_DispatchThreadID)
{
    Values[threadId.x] = float(CountersIn[threadId.x & 15u].y);
}

[shader("compute")]
[numthreads(64, 1, 1)]
void FixedCS(uint3 threadId : SV_DispatchThreadID)
{
    Values[threadId.x] = Fixed[threadId.x & 31u];
}

// Writes the arguments of a later indirect dispatch.
[shader("compute")]
[numthreads(1, 1, 1)]
void ArgsCS(uint3 threadId : SV_DispatchThreadID)
{
    Args[0] = 1u;
    Args[1] = 1u;
    Args[2] = 1u;
}
