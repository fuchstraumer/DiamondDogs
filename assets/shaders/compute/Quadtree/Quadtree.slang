module Quadtree;
import LodestoneAttributes;
import LodestoneShims;

__include QuadtreeCommon;
__include QuadtreeBindings;

bool NodeSubdivides(float2 minXz, float scale, uint level)
{
    if (level >= QuadtreeParams.MaxLevel)
    {
        return false;
    }
    const float2 clampedCam = clamp(QuadtreeParams.CameraPosition.xz, minXz, minXz + scale);
    const float distanceToNode = length(QuadtreeParams.CameraPosition.xz - clampedCam);
    const float lodRange = scale * QuadtreeParams.LodDistanceScale;
    return distanceToNode < lodRange;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void QuadtreeUpdateCS(
    uniform QuadtreePushConstants PushParams,
    uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint level = PushParams.CurrentLevel;
    const uint threadIdx = dispatchThreadId.x;
    const uint nodeCount = NodeCounts[1u + level];
    if (threadIdx >= nodeCount)
    {
        return;
    }

    const uint key = PrevNodes[threadIdx];
    uint3 pos;
    DecodeQuadtreePosition(key, pos);

    const float nodeScale = QuadtreeParams.WorldExtent / float(1u << level);
    const float2 rootOrigin = QuadtreeParams.RootCenter - 0.5f * QuadtreeParams.WorldExtent;
    const float2 minXz = rootOrigin + float2(pos.xy) * nodeScale;
    const float2 maxXz = minXz + nodeScale;

    const float3 minPoint = float3(minXz.x, -QuadtreeParams.MaxWaveHeight, minXz.y);
    const float3 maxPoint = float3(maxXz.x, +QuadtreeParams.MaxWaveHeight, maxXz.y);

    bool culled = false;
    [unroll]
    for (int i = 0; i < 6; ++i)
    {
        const float4 plane = QuadtreeParams.FrustumPlanes[i];
        float3 pMax;
        pMax.x = (plane.x >= 0.0f) ? maxPoint.x : minPoint.x;
        pMax.y = (plane.y >= 0.0f) ? maxPoint.y : minPoint.y;
        pMax.z = (plane.z >= 0.0f) ? maxPoint.z : minPoint.z;
        if (dot(plane.xyz, pMax) + plane.w < 0.0f)
        {
            culled = true;
            break;
        }
    }

    if (culled)
    {
        return;
    }

    const bool shouldSubdivide = NodeSubdivides(minXz, nodeScale, level);

    if (shouldSubdivide)
    {
        uint4 children;
        QuadtreeChildrenFromKey(key, children);
        uint writeIdx;
        if (QUADTREE_USE_WAVE_OPS && QUADTREE_WAVE_SIZE > 0u)
        {
            const uint waveCount = WaveActiveCountBits(true);
            const uint laneOffset = WavePrefixCountBits(true);
            uint waveBase = 0u;
            if (WaveIsFirstLane())
            {
                InterlockedAdd(NodeCounts[2u + level], waveCount * 4u, waveBase);
            }
            waveBase = WaveReadLaneFirst(waveBase);
            writeIdx = waveBase + laneOffset * 4u;
        }
        else
        {
            InterlockedAdd(NodeCounts[2u + level], 4u, writeIdx);
        }

        if (writeIdx + 3u < QuadtreeParams.MaxNodes)
        {
            CurrNodes[writeIdx + 0u] = children.x;
            CurrNodes[writeIdx + 1u] = children.y;
            CurrNodes[writeIdx + 2u] = children.z;
            CurrNodes[writeIdx + 3u] = children.w;
        }
    }
    else
    {
        float morphStart = 1e30f;
        float invMorphRange = 0.0f;
        if (level > 0u)
        {
            const float parentScale = nodeScale * 2.0f;
            const float lodRange = nodeScale * QuadtreeParams.LodDistanceScale;
            const float parentLodRange = parentScale * QuadtreeParams.LodDistanceScale;
            morphStart = lerp(lodRange, parentLodRange, QuadtreeParams.MorphStartRatio);
            const float morphEnd = parentLodRange;
            invMorphRange = 1.0f / max(morphEnd - morphStart, 1e-4f);
        }

        uint edgeMask = 0u;
        if (level > 0u)
        {
            const uint parentLevel = level - 1u;
            const uint2 parentPos = pos.xy >> 1u;
            const float parentScale = nodeScale * 2.0f;

            if ((pos.x & 1u) == 0u && parentPos.x > 0u)
            {
                const int2 nParent = int2(int(parentPos.x) - 1, int(parentPos.y));
                const float2 nMinXz = rootOrigin + float2(nParent) * parentScale;
                if (!NodeSubdivides(nMinXz, parentScale, parentLevel))
                {
                    edgeMask |= 1u;
                }
            }

            if ((pos.x & 1u) == 1u && (parentPos.x + 1u) < (1u << parentLevel))
            {
                const int2 nParent = int2(int(parentPos.x) + 1, int(parentPos.y));
                const float2 nMinXz = rootOrigin + float2(nParent) * parentScale;
                if (!NodeSubdivides(nMinXz, parentScale, parentLevel))
                {
                    edgeMask |= 2u;
                }
            }

            if ((pos.y & 1u) == 0u && parentPos.y > 0u)
            {
                const int2 nParent = int2(int(parentPos.x), int(parentPos.y) - 1);
                const float2 nMinXz = rootOrigin + float2(nParent) * parentScale;
                if (!NodeSubdivides(nMinXz, parentScale, parentLevel))
                {
                    edgeMask |= 4u;
                }
            }

            if ((pos.y & 1u) == 1u && (parentPos.y + 1u) < (1u << parentLevel))
            {
                const int2 nParent = int2(int(parentPos.x), int(parentPos.y) + 1);
                const float2 nMinXz = rootOrigin + float2(nParent) * parentScale;
                if (!NodeSubdivides(nMinXz, parentScale, parentLevel))
                {
                    edgeMask |= 8u;
                }
            }
        }

        uint patchIdx;
        if (QUADTREE_USE_WAVE_OPS && QUADTREE_WAVE_SIZE > 0u)
        {
            const uint waveCount = WaveActiveCountBits(true);
            const uint laneOffset = WavePrefixCountBits(true);
            uint waveBase = 0u;
            if (WaveIsFirstLane())
            {
                InterlockedAdd(NodeCounts[0], waveCount, waveBase);
            }
            waveBase = WaveReadLaneFirst(waveBase);
            patchIdx = waveBase + laneOffset;
        }
        else
        {
            InterlockedAdd(NodeCounts[0], 1u, patchIdx);
        }

        if (patchIdx < QuadtreeParams.MaxSelectedPatches)
        {
            OceanCdlodPatch patch;
            patch.WorldOriginXz = minXz;
            patch.PatchScale = nodeScale;
            patch.LodAndEdgeMask = (level & 0xFu) | ((edgeMask & 0xFu) << 4u);
            patch.MorphStart = morphStart;
            patch.InvMorphRange = invMorphRange;
            patch.Padding = float2(0.0f, 0.0f);

            SelectedPatches.Store(patchIdx, patch);
        }
    }
}

[shader("compute")]
[numthreads(64, 1, 1)]
void QuadtreeBuildIndirectArgsCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x != 0u)
    {
        return;
    }

    const uint patchCount = min(NodeCounts[0], QuadtreeParams.MaxSelectedPatches);

    // VkDrawIndexedIndirectCommand:
    // indexCount = 64 * 64 * 6 = 24576
    // instanceCount = patchCount
    // firstIndex = 0
    // vertexOffset = 0
    // firstInstance = 0
    IndirectDrawArgs[0] = 64u * 64u * 6u;
    IndirectDrawArgs[1] = patchCount;
    IndirectDrawArgs[2] = 0u;
    IndirectDrawArgs[3] = 0u;
    IndirectDrawArgs[4] = 0u;

    // Reset counters for the next dispatch:
    // patchCount = 0
    // nodeCount = 1 (seed root node)
    // child counts for level 1..31 = 0
    NodeCounts[0] = 0u;
    NodeCounts[1] = 1u;
    [unroll]
    for (uint i = 2u; i < 32u; ++i)
    {
        NodeCounts[i] = 0u;
    }

    // Reset root node (level 0, x=0, y=0 -> key = 0) in CurrNodes
    CurrNodes[0] = 0u;
}
