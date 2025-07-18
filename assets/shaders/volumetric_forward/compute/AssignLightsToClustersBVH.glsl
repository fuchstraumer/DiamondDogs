#include "Structures.glsl"
#include "Functions.glsl"

#pragma USE_RESOURCES VolumetricForward
#pragma USE_RESOURCES SortResources
#pragma USE_RESOURCES BVHResources
#pragma USE_RESOURCES VolumetricForwardLights

const uint NUM_THREADS = 32u;
layout (local_size_x = NUM_THREADS, local_size_y = 1u, local_size_z = 1u) in;
const uint MaxLights = 2048u;
const int NODE_STACK_LIMITS = 1024;

const uint NumChildNodes[6] = {
    1u,
    33u,
    1057u,
    33825u,
    1082401u,
    34636833u
};

// 32 layers of nodes
shared uint gs_NodeStack[1024u]; 
// Current index in the above node stack
shared int gs_StackPtr;
// The index of the parent node of the BVH node currently being processed
shared uint gs_ParentIdx;

shared uint gs_ClusterIndex1D;
shared AABB gs_ClusterAABB;
shared uint gs_PointLightCount;
shared uint gs_SpotLightCount;
shared uint gs_PointLightStartOffset;
shared uint gs_SpotLightStartOffset;
shared uint gs_PointLightList[MaxLights];
shared uint gs_SpotLightList[MaxLights];
shared bool gs_NonZeroLevels;

void PushNode(in const uint nodeIndex)
{
    int prevPtr = atomicAdd(gs_StackPtr, 1);
    if (prevPtr < NODE_STACK_LIMITS)
    {
        gs_NodeStack[prevPtr] = nodeIndex;
    }
}

uint PopNode()
{
    int prevPtr = atomicAdd(gs_StackPtr, -1);
    return (prevPtr > 0 && prevPtr < 1024) ? gs_NodeStack[prevPtr - 1] : 0u;
}

uint GetFirstChild()
{
    return (gs_ParentIdx * 32u) + 1u;
}

bool IsLeafNode(uint childIndex, const in uint numLevels)
{
    return childIndex > (NumChildNodes[numLevels - 1u] - 1u);
}

uint GetLeafIndex(uint nodeIndex, const in uint numLevels)
{
    return nodeIndex - NumChildNodes[numLevels - 1];
}

void main()
{
    uint childOffset = gl_LocalInvocationIndex;

    if (childOffset == 0u)
    {
        gs_PointLightCount = 0u;
        gs_SpotLightCount = 0u;
        gs_StackPtr = 0;
        gs_ParentIdx = 0u;
        gs_ClusterIndex1D = imageLoad(UniqueClusters, int(gl_WorkGroupID.x)).r;
        gs_ClusterAABB.Min = ClusterAABBs.Data[gs_ClusterIndex1D].Min;
        gs_ClusterAABB.Max = ClusterAABBs.Data[gs_ClusterIndex1D].Max;
        gs_NonZeroLevels = BVHParams.PointLightLevels != 0u;
        PushNode(0u);
    }

    groupMemoryBarrier();
    memoryBarrierShared();
    barrier();

    if (gs_NonZeroLevels)
    {
        for (;;)
        {
            uint childIndex = GetFirstChild() + childOffset;
            if (IsLeafNode(childIndex, BVHParams.SpotLightLevels))
            {
                uint leafIndex = GetLeafIndex(childIndex, BVHParams.SpotLightLevels);
                if (leafIndex < LightCounts.NumSpotLights)
                {
                    uint lightIndex = imageLoad(SpotLightIndices, int(leafIndex)).r;
                    Sphere sphere;
                    sphere.c = SpotLights.Data[lightIndex].PositionViewSpace.xyz;
                    sphere.r = SpotLights.Data[lightIndex].Range;
                    if (SphereInsideAABB_Fast(sphere, gs_ClusterAABB) && SpotLights.Data[lightIndex].Enabled)
                    {
                        uint idx = atomicAdd(gs_SpotLightCount, 1u);
                        if (idx < MaxLights)
                        {
                            gs_SpotLightList[idx] = lightIndex;
                        }
                    }
                }
            }
            else if (AABBIntersectAABB(gs_ClusterAABB, SpotLightBVH.Data[childIndex]))
            {
                PushNode(childIndex);
            }

            groupMemoryBarrier();
            memoryBarrierShared();
            barrier();

            if (childOffset == 0u)
            {
                gs_ParentIdx = PopNode();
            }
                
            groupMemoryBarrier();
            memoryBarrierShared();
            barrier();

            if (gs_ParentIdx == 0u)
            {
                break;
            }

        }
    }


    groupMemoryBarrier();
    memoryBarrierShared();
    barrier();

    if (childOffset == 0u)
    {
        gs_StackPtr = 0;
        gs_ParentIdx = 0u;
        PushNode(0u);
        gs_NonZeroLevels = BVHParams.SpotLightLevels != 0u;
    }

    groupMemoryBarrier();
    memoryBarrierShared();
    barrier();

    if (gs_NonZeroLevels)
    {

        for(;;)
        {
            uint childIndex = GetFirstChild() + childOffset;
            if (IsLeafNode(childIndex, BVHParams.PointLightLevels))
            {
                uint leafIndex = GetLeafIndex(childIndex, BVHParams.PointLightLevels);
                if (leafIndex < LightCounts.NumPointLights)
                {
                    uint light_index = imageLoad(PointLightIndices, int(leafIndex)).r;
                    Sphere sphere;
                    sphere.c = PointLights.Data[light_index].PositionViewSpace.xyz;
                    sphere.r = PointLights.Data[light_index].Range;
                    if (SphereInsideAABB_Fast(sphere, gs_ClusterAABB) && PointLights.Data[light_index].Enabled)
                    {
                        uint idx = atomicAdd(gs_PointLightCount, 1u);
                        if (idx < MaxLights)
                        {
                            gs_PointLightList[idx] = light_index;
                        }
                    }
                }
            }
            else if (AABBIntersectAABB(gs_ClusterAABB, PointLightBVH.Data[childIndex]))
            {
                PushNode(childIndex);
            }

            groupMemoryBarrier();
            memoryBarrierShared();
            barrier();

            if (childOffset == 0u)
            {
                gs_ParentIdx = PopNode();
            }

            groupMemoryBarrier();
            memoryBarrierShared();
            barrier();

            if (gs_ParentIdx == 0u)
            {
                break;
            }
        }
    }

    groupMemoryBarrier();
    memoryBarrierShared();
    barrier();

    if (childOffset == 0u)
    {
        gs_PointLightStartOffset = imageAtomicAdd(PointLightIndexCounter, int(0), gs_PointLightCount);
        imageStore(PointLightGrid, int(gs_ClusterIndex1D), uvec4(gs_PointLightStartOffset, gs_PointLightCount, 0u, 0u));
        gs_SpotLightStartOffset = imageAtomicAdd(SpotLightIndexCounter, int(0), gs_SpotLightCount);
        imageStore(SpotLightGrid, int(gs_ClusterIndex1D), uvec4(gs_SpotLightStartOffset, gs_SpotLightCount, 0u, 0u));
    }

    groupMemoryBarrier();
    memoryBarrierShared();
    barrier();

    for (uint i = childOffset; i < gs_PointLightCount; i += 32u)
    {
        imageStore(PointLightIndexList, int(gs_PointLightStartOffset + i), uvec4(gs_PointLightList[i], 0u, 0u, 0u));
    }

    for (uint i = childOffset; i < gs_SpotLightCount; i += 32u)
    {
        imageStore(SpotLightIndexList, int(gs_SpotLightStartOffset + i), uvec4(gs_SpotLightList[i], 0u, 0u, 0u));
    }
}