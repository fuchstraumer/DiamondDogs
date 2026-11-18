implementing PlanetLab;
__include Buffers;

Counters PlateFrontPointCount;
Counters PlateFrontPointCursor;
Words PlateFrontPointOffset;
Ints FrontPoint;
Floats FrontPointPosition;
Ints CellFrontPoint;
Floats CellDistance;

void StoreFloat3(uint where, float3 value)
{
    FrontPointPosition.Store(where * 3u, value.x);
    FrontPointPosition.Store(where * 3u + 1u, value.y);
    FrontPointPosition.Store(where * 3u + 2u, value.z);
}

float3 LoadFloat3(uint where)
{
    float3 value;
    value.x = FrontPointPosition.Load(where * 3u);
    value.y = FrontPointPosition.Load(where * 3u + 1u);
    value.z = FrontPointPosition.Load(where * 3u + 2u);
    return value;
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
    FrontPoint.Store(slot, int(point));
    StoreFloat3(slot, position);
}

[shader("compute")]
[numthreads(64, 1, 1)]
void NearestFrontPointCS(uint3 threadId : SV_DispatchThreadID)
{
    // The brute force search to find nearest points to the subducting points: our initial
    // thought was to use jump flooding for this, but given our point count (fairly small),
    // a simple brute force approach is likely more than sufficient for our needs.
    const uint idxLimit = PlateLiveOffset.Load(Params.PlateCount);
    if (threadId.x >= idxLimit)
    {
        return;
    }

    const uint flattenedPt = LiveCell.Load(threadId.x);

    const uint plate = flattenedPt / Params.PointCount;
    const uint plateCell = flattenedPt % Params.PointCount;
    // remember: we store plates in their own local coordinate systems, as even the python
    // prototype identified real precision issues otherwise
    const float3x3 plateTransform = LoadRotation(plate);
    const float3 latticePoint = LatticePoint(int(plateCell), int(Params.PointCount));
    const float3 worldPoint = mul(plateTransform, latticePoint);

    // walk the range we built in the previous step - use the dot product
    // to calculate the distance, and try and find the best/earliest point
    float best = 3.402823466e+38f;
    int bestFrontPoint = -1;
    const uint begin = PlateFrontPointOffset.Load(plate);
    const uint end = PlateFrontPointOffset.Load(plate + 1u);

    for (uint slot = begin; slot < end; ++slot)
    {
        float3 position = LoadFloat3(slot);
        const float3 delta = worldPoint - position;
        const float distance = dot(delta, delta);

        const int index = FrontPoint.Load(slot);
        if (distance < best || (distance == best && index < bestFrontPoint))
        {
            best = distance;
            bestFrontPoint = index;
        }
    }

    CellFrontPoint.Store(flattenedPt, bestFrontPoint);
    CellDistance.Store(flattenedPt, asfloat(0x7f800000));
    if (bestFrontPoint >= 0)
    {
        const float bestDistance = 6370.0f * 2.0f * asin(clamp(0.5f * sqrt(best), 0.0f, 1.0f));
        CellDistance.Store(flattenedPt, bestDistance);
    }

}
