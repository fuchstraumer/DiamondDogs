implementing PlanetLab;
__include Buffers;
__include HEALPix;

/*
    TorqueSums treats each plate as a rigid body and computes the torque balance needs to be to move the
    plates during the sim. This math is run as fixed point arithmetic, because atomic operations on 
    floats change with ordering. This only holds up to ~130k points: more would need int64 atomics.
*/

static const float PositionSumScale = 16384.0f;
static const float PositionSumScaleInv = 1.0f / PositionSumScale;
static const float PullScale = 4096.0f;
static const float SlabFullAgeMy = 80.0f;
static const float TimeStepMy = 2.0f;
static const uint RowGroupSize = 64u;

[shader("compute")]
[numthreads(64, 1, 1)]
void SumPullCS(uint3 threadID : SV_DispatchThreadID)
{
    // this only has to run once per front points, so those are what make the "pull" forces
    // for each plates torque balance
    const uint pointID = threadID.x;
    if (pointID >= StepParams.PointCount)
    {
        return;
    }

    const FrontState frontState = LoadFrontStateIn(pointID);
    if (frontState.GetSubduction() != 1u)
    {
        return;
    }

    // torque balance is influenced by the plate going underneath this front subduction point
    const OwnersData owners = LoadOwnersIn(pointID);
    const uint underPlate = uint(owners.GetSecondPlate());
    const int4 plateCounters = LoadPlateCountersIn(underPlate);
    const uint cellCount = uint(plateCounters.w);
    if (cellCount == 0u)
    {
        return;
    }

    // now compute the centroid, and figure out how much trench this point stands for. 
    // the centroid comes from the first dispatch: we get it back into floating point,
    // average it, and then rotate it to the world frame. we then adjust the trench amount
    // based on the plates speed and age
    const int3 positionSumInt = plateCounters.xyz;
    const float3 positionSum = (float3(positionSumInt) * PositionSumScaleInv);

    const float3 centroidLocal = positionSum / float(cellCount);
    const float3x4 plateFrame = LoadRotation(underPlate);
    const float3 centroidWorld = PlateToWorld(centroidLocal, plateFrame);

    const float cellArea = 4.0f * 3.14159265f * PlanetRadiusKm * (PlanetRadiusKm / float(StepParams.PointCount));
    const float effectiveTravel = max(frontState.GetSpeed(), 10.0f) * StepParams.TimeStepMy;
    const float trenchAmount = cellArea / max(effectiveTravel, 1.0f);
    const float slabWeight = trenchAmount * sqrt(clamp(frontState.GetSlabAge() / SlabFullAgeMy, 0.0f, 1.0f));

    // now apply the pull and perform the last fixed-point add. 
    // cross(centroid, world) is the rotation axis/lever arm that would turn the plate towards the trench
    const float3 worldPoint = HealpixPixToVec(pointID, StepParams.HealpixOrder);
    const float3 axis = cross(centroidWorld, worldPoint);
    const float3 pullForce = axis / max(length(axis), 1e-6f) * slabWeight;
    const int3 intPull = int3(round(pullForce * PullScale));
    AddPlatePull(underPlate * 3u, intPull);
}