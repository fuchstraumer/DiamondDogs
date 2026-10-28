implementing Quadtree;
import LodestoneAttributes;
import LodestoneShims;

[ls_axis_boolean][ls_axis_kind("capability")]
extern static const bool QUADTREE_USE_WAVE_OPS = true;

[ls_axis_values("16, 32, 64, 128")][ls_axis_kind("tuning")][ls_axis_active_when("QUADTREE_USE_WAVE_OPS==1")]
extern static const uint QUADTREE_WAVE_SIZE = 32u;

public struct QuadtreePushConstants
{
    public uint CurrentLevel;
};

public struct OceanPatchVertex
{
    public float2 GridUv;
};

public struct OceanCdlodPatch
{
    public float2 WorldOriginXz;
    public float PatchScale;
    public uint LodAndEdgeMask;
    public float MorphStart;
    public float InvMorphRange;
    public float2 Padding;
};

public extern struct SelectedPatchesRwBuffer : IReadWriteBuffer<OceanCdlodPatch>;

public SelectedPatchesRwBuffer SelectedPatches;

public struct OceanQuadtreeParams
{
    public float4 FrustumPlanes[6];
    public float3 CameraPosition;
    public float WorldExtent;
    public float2 RootCenter;
    public float MaxWaveHeight;
    public float MorphStartRatio;
    public uint MaxLevel;
    public uint BaseGridCount;
    public float LodDistanceScale;
    public uint MaxNodes;
    public uint MaxSelectedPatches;
    public uint Pad0;
    public uint Pad1;
    public uint Pad2;
};

public ConstantBuffer<OceanQuadtreeParams> QuadtreeParams;
public StructuredBuffer<uint> PrevNodes;
public RWStructuredBuffer<uint> CurrNodes;
[ls_element_count("32")]
public RWStructuredBuffer<uint> NodeCounts;
[ls_resource_role("IndirectDrawIndexed")]
[ls_element_count("5")]
public RWStructuredBuffer<uint> IndirectDrawArgs;
