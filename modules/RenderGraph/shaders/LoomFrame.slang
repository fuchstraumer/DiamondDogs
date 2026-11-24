module LoomFrame;

// Loom's frame constants. modules/RenderGraph/include/LoomFrame.hpp is the C++ mirror: keep the two equal. Loom binds
// g_LoomFrame to every shader that declares it, with no Bind statement, and writes it each Tick. A cook that imports
// this module adds LOOM_SHADER_DIR to its INCLUDE_DIRS.

public static const uint k_LoomMaxFrameTargets = 4;

// One frame target, in import order
public struct LoomTargetInfo
{
    public uint2 TargetExtent;
    public uint2 AllocationExtent;
    public uint2 RenderExtent;
    public float2 RenderToAllocation;   // RenderExtent / AllocationExtent: the UV scale of a scene image
};

public struct LoomFrame
{
    public float Time;                  // seconds since Realize
    public float DeltaTime;
    public uint FrameIndex;
    public uint TargetCount;
    public LoomTargetInfo Targets[k_LoomMaxFrameTargets];
};

public ConstantBuffer<LoomFrame> g_LoomFrame;
