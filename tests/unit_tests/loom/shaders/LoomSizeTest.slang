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

// Images. Albedo states a 64x32 extent and Small a 16x16 one. Ids samples as uint, Volume is 3D, and Lit is an rgba32f
// storage image that states no extent.

[ls_extent_2d("64", "32")]
Texture2D<float4> Albedo;

[ls_extent_2d("16", "16")]
Texture2D<float4> Small;

Texture2D<uint> Ids;

Texture3D<float4> Volume;

[format("rgba32f")]
RWTexture2D<float4> Lit;

[shader("compute")]
[numthreads(8, 8, 1)]
void ShadeCS(uint3 threadId : SV_DispatchThreadID)
{
    const float id = float(Ids.Load(int3(threadId.xy, 0)));
    Lit[threadId.xy] = Albedo.Load(int3(threadId.xy, 0)) + id;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void SmallCS(uint3 threadId : SV_DispatchThreadID)
{
    Lit[threadId.xy] = Small.Load(int3(threadId.xy, 0));
}

[shader("compute")]
[numthreads(8, 8, 1)]
void VolumeCS(uint3 threadId : SV_DispatchThreadID)
{
    Lit[threadId.xy] = Volume.Load(int4(threadId.xy, 0, 0));
}

// Array images. The third value of ls_extent_3d on an array texture is its layer count (Lodestone C31): Layered states
// 16x16 with 3 layers, and Layered4 the same extent with 4 layers.

[ls_extent_3d("16", "16", "3")]
Texture2DArray<float4> Layered;

[ls_extent_3d("16", "16", "4")]
Texture2DArray<float4> Layered4;

[shader("compute")]
[numthreads(8, 8, 1)]
void LayeredCS(uint3 threadId : SV_DispatchThreadID)
{
    Lit[threadId.xy] = Layered.Load(int4(threadId.xy, 2, 0));
}

[shader("compute")]
[numthreads(8, 8, 1)]
void Layered4CS(uint3 threadId : SV_DispatchThreadID)
{
    Lit[threadId.xy] = Layered4.Load(int4(threadId.xy, 3, 0));
}
