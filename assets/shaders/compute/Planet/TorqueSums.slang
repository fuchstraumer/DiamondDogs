implementing PlanetLab;
__include Buffers;
__include Fibonacci;

/*
    TorqueSums treats each plate as a rigid body and computes the torque balance needs to be to move the
    plates during the sim. This math is run as fixed point arithmetic, because atomic operations on 
    floats change with ordering. This only holds up to ~130k points: more would need int64 atomics.
*/

SignedCounters PlatePositionSum;
SignedCounters PlatePull;
Counters PlateCellCount;
Counters PlateContinentalCount;
static const float PositionSumScale = 16384.0f;
static const float PositionSumScaleInv = 1.0f / PositionSumScale;
static const float PullScale = 4096.0f;
static const float SlabFullAgeMy = 80.0f;
static const float TimeStepMy = 2.0f;
static const uint RowGroupSize = 64u;

void AddPlatePositionSum(uint where, int3 position)
{
    PlatePositionSum.Add(where, position.x);
    PlatePositionSum.Add(where + 1u, position.y);
    PlatePositionSum.Add(where + 2u, position.z);
}

int3 LoadPlatePositionSum(uint where)
{
    int3 position;
    position.x = PlatePositionSum.Load(where);
    position.y = PlatePositionSum.Load(where + 1u);
    position.z = PlatePositionSum.Load(where + 2u);
    return position;
}

void AddPlatePull(uint where, int3 pull)
{
    PlatePull.Add(where, pull.x);
    PlatePull.Add(where + 1u, pull.y);
    PlatePull.Add(where + 2u, pull.z);
}

int3 LoadPlatePull(uint where)
{
    int3 pull;
    pull.x = PlatePull.Load(where);
    pull.y = PlatePull.Load(where + 1u);
    pull.z = PlatePull.Load(where + 2u);
    return pull;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void SumPlatesCS(uint3 groupID : SV_GroupID, uint3 laneInGroup : SV_GroupThreadID)
{
    const uint groupsPerPlate = (Params.PointCount + RowGroupSize - 1u) / RowGroupSize;
    const uint plateID = groupID.x / groupsPerPlate;
    const uint cellID = (groupID.x % groupsPerPlate) * RowGroupSize + laneInGroup.x;

    bool isAlive = false;
    bool isContinental = false;

    int3 position = int3(0, 0, 0);
    if (cellID < Params.PointCount)
    {
        const uint flattenedID = plateID * Params.PointCount + cellID;
        isAlive = LoadAlive(flattenedID) != 0u;
        isContinental = isAlive && LoadContinental(flattenedID) != 0u;

        if (isAlive)
        {
            const int latticeCount = int(Params.PointCount) * int(PositionSumScale);
            const float3 floatPos = LatticePoint(int(cellID), int(Params.PointCount)) * PositionSumScale.xxx;
            position = int3(round(floatPos));
        }
    }

    const uint cellCount = WaveActiveCountBits(isAlive);
    const uint continentalCount = WaveActiveCountBits(isContinental);
    const int3 positionSum = WaveActiveSum(position);

    if (WaveIsFirstLane())
    {
        PlateCellCount.Add(plateID, cellCount);
        PlateContinentalCount.Add(plateID, continentalCount);
        AddPlatePositionSum(plateID * 3u, positionSum);
    }
}

[shader("compute")]
[numthreads(64, 1, 1)]
void SumPullCS(uint3 threadID : SV_DispatchThreadID)
{
    // this only has to run once per front points, so those are what make the "pull" forces
    // for each plates torque balance
    const uint pointID = threadID.x;
    if (pointID >= Params.PointCount || FrontSubduction.Load(pointID) != 1u)
    {
        return;
    }

    // torque balance is influenced by the plate going underneath this front subduction point
    const uint underPlate = uint(SecondPlate.Load(pointID));
    const uint cellCount = PlateCellCount.Load(underPlate);
    if (cellCount == 0u)
    {
        return;
    }

    // now compute the centroid, and figure out how much trench this point stands for. 
    // the centroid comes from the first dispatch: we get it back into floating point,
    // average it, and then rotate it to the world frame. we then adjust the trench amount
    // based on the plates speed and age
    const int3 positionSumInt = LoadPlatePositionSum(underPlate * 3u);
    const float3 positionSum = (float3(positionSumInt) * PositionSumScaleInv);

    const float3 centroidLocal = positionSum / float(cellCount);
    const float3x3 plateFrame = LoadRotation(underPlate);
    const float3 centroidWorld = mul(plateFrame, centroidLocal);

    const float cellArea = 4.0f * 3.14159265f * PlanetRadiusKm * (PlanetRadiusKm / float(Params.PointCount));
    const float trenchAmount = cellArea / max(FrontSpeed.Load(pointID) * TimeStepMy, sqrt(cellArea));
    const float slabWeight = trenchAmount * sqrt(clamp(FrontSlabAge.Load(pointID) / SlabFullAgeMy, 0.0f, 1.0f));

    // now apply the pull and perform the last fixed-point add. 
    // cross(centroid, world) is the rotation axis/lever arm that would turn the plate towards the trench
    const float3 worldPoint = LatticePoint(int(pointID), int(Params.PointCount));
    const float3 axis = cross(centroidWorld, worldPoint);
    const float3 pullForce = axis / max(length(axis), 1e-6f) * slabWeight;
    const int3 intPull = int3(round(pullForce * PullScale));
    AddPlatePull(underPlate * 3u, intPull);
}