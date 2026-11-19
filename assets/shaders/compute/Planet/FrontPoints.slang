implementing PlanetLab;
__include Buffers;

Counters PlateFrontPointCount;
Counters PlateFrontPointCursor;
Words PlateFrontPointOffset;
Ints CellFrontPoint;
Floats CellDistance;
Points FrontPoints;

static const uint SearchUnrollDepth = 4u;

struct NearestCandidate
{
    float Distance;
    int Index;
};

NearestCandidate NoCandidate()
{
    NearestCandidate candidate;
    candidate.Distance = 3.402823466e+38f; // maximum float value
    candidate.Index = -1;
    return candidate;
}

NearestCandidate MakeCandidate(float3 worldPoint, float4 frontPoint)
{
    const float3 delta = worldPoint - frontPoint.xyz;
    NearestCandidate candidate;
    candidate.Distance = dot(delta, delta);
    candidate.Index = int(asuint(frontPoint.w));
    return candidate;
}

NearestCandidate Nearest(NearestCandidate lhs, NearestCandidate rhs)
{
    const bool takeRhs = rhs.Distance < lhs.Distance || (rhs.Distance == lhs.Distance && rhs.Index < lhs.Index);
    return takeRhs ? rhs : lhs;
}

void StoreFrontPointData(uint where, float3 position, uint point)
{
    FrontPoints.Store(where, float4(position, asfloat(point)));
}

/*
    The job of these entry points is to process all the points on subduction fronts,
    where one plate is diving underneath another. These are the points from previous 
    stages where we set FrontSubduction to 1. At N=100k, there are about 1200 of these
    (so there are relatively few of them compared to the total number of points, which
    is to be expected in a plate tectonics simulation if you think about it - most points
    are just on plates floating on the mantle!).
*/

[shader("compute")]
[numthreads(64, 1, 1)]
void CountFrontPointsCS(uint3 threadId : SV_DispatchThreadID)
{
    const uint point = threadId.x;
    if (point >= Params.PointCount || FrontSubduction.Load(point) != 1u)
    {
        return;
    }

    int plate = Pass3Plate.Load(point);
    PlateFrontPointCount.Add(uint(plate), 1u);
}

[shader("compute")]
[numthreads(1, 1, 1)]
void OffsetFrontPointsCS(uint3 threadId : SV_DispatchThreadID)
{
    uint sum = 0u;
    for (uint plate = 0u; plate < Params.PlateCount; ++plate)
    {
        PlateFrontPointOffset.Store(plate, sum);
        sum += PlateFrontPointCount.Load(plate);
    }

    PlateFrontPointOffset.Store(Params.PlateCount, sum);
}

[shader("compute")]
[numthreads(64, 1, 1)]
void ScatterFrontPointsCS(uint3 threadId : SV_DispatchThreadID)
{
    const uint point = threadId.x;

    // skip points out of range or not on a subduction front
    if (point >= Params.PointCount || FrontSubduction.Load(point) != 1u)
    {
        return;
    }

    // determine which slot this current point goes into - then convert it from the flattened
    // integral slot in the lattice to an actual 3D point in space: write that out, along
    // with it's point index into a dense array of front points for further processing.
    const uint plate = uint(Pass3Plate.Load(point));
    // `PlateFrontPointCursor` is an atomic counter, so multiple threads can increment it concurrently
    // to write to the same plate's front point slots without causing race conditions.
    const uint slot = PlateFrontPointOffset.Load(plate) + PlateFrontPointCursor.Add(plate, 1u);
    const float3 position = LatticePoint(int(point), int(Params.PointCount));
    StoreFrontPointData(slot, position, point);
}

static const uint SearchBlockSize = 64u;
groupshared float4 SharedFrontPoints[SearchBlockSize];

[shader("compute")]
[numthreads(64, 1, 1)]
void NearestFrontPointCS(uint3 threadId : SV_DispatchThreadID,
                         uint groupID : SV_GroupID,
                         uint laneInGroup : SV_GroupThreadID)
{
    // The brute force search to find nearest points to the subducting points: our initial
    // thought was to use jump flooding for this, but given our point count (fairly small),
    // a simple brute force approach is likely more than sufficient for our needs.

    // every lane now takes part in the groupshared loads and barriers - so a lane past
    // the end of the list is aware of this, but doesn't return since it has to help still
    const uint idxLimit = PlateLiveOffset.Load(Params.PlateCount);
    const bool isLive = threadId.x < idxLimit;

    // the live list is in plate order, and so are the front points: the plates of a groups
    // first and last cells bound one contiguous range of slots, even if the group spans multiple plates
    const uint groupFirstCell = groupID.x * SearchBlockSize;
    const uint groupLastCell = min(groupFirstCell + SearchBlockSize, idxLimit) - 1u;
    const uint firstPlate = LiveCell.Load(groupFirstCell) / Params.PointCount;
    const uint sharedBegin = PlateFrontPointOffset.Load(firstPlate);
    const uint lastPlate = (LiveCell.Load(groupLastCell) / Params.PointCount) + 1u;
    const uint sharedEnd = PlateFrontPointOffset.Load(lastPlate);

    uint flattenedPoint = 0u;
    float3 worldPoint = float3(0.0f, 0.0f, 0.0f);
    uint begin = 0u;
    uint end = 0u;

    if (isLive)
    {
        flattenedPoint = LiveCell.Load(threadId.x);
        const uint plateIdx = flattenedPoint / Params.PointCount;
        const uint plateCell = flattenedPoint % Params.PointCount;
        const float3x4 plateTransform = LoadRotation(plateIdx);
        const float3 latticePoint = LatticePoint(int(plateCell), int(Params.PointCount));
        worldPoint = PlateToWorld(latticePoint, plateTransform);
        begin = PlateFrontPointOffset.Load(plateIdx);
        end = PlateFrontPointOffset.Load(plateIdx + 1u);
    }

    NearestCandidate nearest0 = NoCandidate();
    NearestCandidate nearest1 = NoCandidate();
    NearestCandidate nearest2 = NoCandidate();
    NearestCandidate nearest3 = NoCandidate();

    for (uint sharedStart = sharedBegin; sharedStart < sharedEnd; sharedStart += SearchBlockSize)
    {
        const uint loadSlot = sharedStart + laneInGroup.x;
        if (loadSlot < sharedEnd)
        {
            SharedFrontPoints[laneInGroup.x] = FrontPoints.Load(loadSlot);
        }

        GroupMemoryBarrierWithGroupSync();

        // check this lanes own plate range, inside this shared block!
        const uint first = max(begin, sharedStart);
        const uint last = min(end, min(sharedStart + SearchBlockSize, sharedEnd));

        uint slot = first;
        for(; slot + SearchUnrollDepth <= last; slot += SearchUnrollDepth)
        {
            const uint sharedSlot = slot - sharedStart;
            nearest0 = Nearest(nearest0, MakeCandidate(worldPoint, SharedFrontPoints[sharedSlot]));
            nearest1 = Nearest(nearest1, MakeCandidate(worldPoint, SharedFrontPoints[sharedSlot + 1]));
            nearest2 = Nearest(nearest2, MakeCandidate(worldPoint, SharedFrontPoints[sharedSlot + 2]));
            nearest3 = Nearest(nearest3, MakeCandidate(worldPoint, SharedFrontPoints[sharedSlot + 3]));
        }

        for (; slot < last; ++slot)
        {
            nearest0 = Nearest(nearest0, MakeCandidate(worldPoint, SharedFrontPoints[slot - sharedStart]));
        }

        GroupMemoryBarrierWithGroupSync();
    }

    // *now* "dead" cell threads can exit, as they have no further work to help with
    if (!isLive)
    {
        return;
    }

    const NearestCandidate best = Nearest(Nearest(nearest0, nearest1), Nearest(nearest2, nearest3));

    CellFrontPoint.Store(flattenedPoint, best.Index);
    CellDistance.Store(flattenedPoint, asfloat(0x7f800000));

    if (best.Index >= 0)
    {
        const float bestDistance = 6370.0f * 2.0f * asin(clamp(0.5f * sqrt(best.Distance), 0.0f, 1.0f));
        CellDistance.Store(flattenedPoint, bestDistance);
    }

}
