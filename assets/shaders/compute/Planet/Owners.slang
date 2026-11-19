implementing PlanetLab;
__include Fibonacci;
__include Buffers;

// Pass 1 of the plate port: for each world point, the best two plates that have crust there.
// Reference: tectonics.FindOwners. Each thread reads shared buffers and writes only its own four values.

struct OwnersParams
{
    uint PointCount;
    uint PlateCount;
};

ConstantBuffer<OwnersParams> Params;

// [plate][row][column], row-major, 9 floats for each plate
FloatsIn Rotation;
IntsIn PreviousPlate;

Ints BestPlate;
Ints BestCell;
Ints SecondPlate;
Ints SecondCell;

static const float ContinentPriority = 1.0e6f;
static const float NoScore = -3.402823466e+38f;

static const uint OwnersGroupSize = 64u;
static const uint MaxCachedPlates = 64u;

// xyz = world-space cap center, w = cosLimit
groupshared float4 SharedWorldCaps[MaxCachedPlates];
// these are float3 in order to purposefully try and push them away from bank conflict territory
groupshared float3 SharedRotationR0[MaxCachedPlates];
groupshared float3 SharedRotationR1[MaxCachedPlates];
groupshared float3 SharedRotationR2[MaxCachedPlates];

bool InsideCap(in float3 direction,
               in float3 center,
               in float spread,
               in float cosMargin,
               in float sinMargin);

[shader("compute")]
[numthreads(64, 1, 1)]
void OwnersCS(uint3 threadId : SV_DispatchThreadID, uint3 laneInGroup : SV_GroupThreadID)
{
    const uint point = threadId.x;
    const int count = int(Params.PointCount);
    const int plateCount = Params.PlateCount;

    const float marginAmount = 2.0f * sqrt(4.0f * 3.141592653589793f / float(count));
    float sinMargin, cosMargin;
    sincos(marginAmount, sinMargin, cosMargin);

    for (uint plate = laneInGroup.x; plate < plateCount; plate += OwnersGroupSize)
    {
        const float3x4 rotation = LoadRotation(plate);
        SharedRotationR0[plate] = rotation[0].xyz;
        SharedRotationR1[plate] = rotation[1].xyz;
        SharedRotationR2[plate] = rotation[2].xyz;

        const float4 capCenter = PlateCapCenters.Load(plate);
        
        float3 worldCenter = 0.0f.xxx;
        float cosLimit = -2.0f; // PlateCapNone: always pass

        if (capCenter.w == PlateCapEmpty)
        {
            cosLimit = 2.0f; // always cull
        }
        else if (capCenter.w == PlateCapValid)
        {
            worldCenter = PlateToWorld(capCenter.xyz, rotation);
            const float spread = asfloat(PlateCapSpreadIn.Load(plate));
            const float cosRadius = 1.0f - spread;
            if (cosRadius > -cosMargin)
            {
                const float sinRadius = sqrt(max(1.0f - cosRadius * cosRadius, 0.0f));
                cosLimit = cosRadius * cosMargin - sinRadius * sinMargin;
            }
        }

        SharedWorldCaps[plate] = float4(worldCenter, cosLimit);
    }

    GroupMemoryBarrierWithGroupSync();

    // now we let the out-of-range lanes exit, since they're not helpers anymore
    if (threadId.x >= Params.PointCount)
    {
        return;
    }

    // todo-ship: store point indices in morton-order so all 32 lanes in a warp will be in a tight 2D patch,
    // as right now they're effectively covering a 360 degree ring
    const float3 worldPoint = LatticePoint(int(point), count);
    const int previousPlate = PreviousPlate.Load(point);

    float bestScore = NoScore;
    float secondScore = NoScore;
    int2 best = -1.xx;
    int2 second = -1.xx;

    for (uint plate = 0u; plate < plateCount; ++plate)
    {
        const float4 worldCap = SharedWorldCaps[plate];
        if (dot(worldPoint, worldCap.xyz) < worldCap.w)
        {
            continue; // skip this plate as the point is outside the cap
        }

        // only compute world to plate and nearest lattice point if the prev check passed
        // (previously we didn't used to run that, it's another good filter)
        const float3 frameDirection =
            worldPoint.x * SharedRotationR0[plate] +
            worldPoint.y * SharedRotationR1[plate] +
            worldPoint.z * SharedRotationR2[plate];

        const int cell = FibonacciNearest(frameDirection, int(count));
        const uint flattenedIdx = plate * Params.PointCount + uint(cell);

        if (LoadAlive(flattenedIdx) == 0u)
        {
            continue;
        }

        float score = CrustBirth.Load(flattenedIdx);
        if (LoadContinental(flattenedIdx) != 0u)
        {
            // augment score by continent priority
            score = ContinentPriority + ((previousPlate == int(plate)) ? 0.5f : 0.0f);
        }

        if (score > bestScore)
        {
            second = best;
            secondScore = bestScore;
            best = int2(int(plate), cell);
            bestScore = score;
        }
        else if (score > secondScore)
        {
            second = int2(int(plate), cell);
            secondScore = score;
        }
    }

    // store the best and second best scores for this point
    BestPlate.Store(point, best.x);
    BestCell.Store(point, best.y);
    SecondPlate.Store(point, second.x);
    SecondCell.Store(point, second.y);
}

bool InsideCap(in float3 direction,
               in float3 center,
               in float spread,
               in float cosMargin,
               in float sinMargin)
{
    const float cosRadius = 1.0f - spread;
    // the radius plus the margin reaches past the far pole, every direction is inside
    if (cosRadius <= -cosMargin)
    {
        return true;
    }

    const float sinRadius = sqrt(max(1.0f - cosRadius * cosRadius, 0.0f));
    const float cosLimit = cosRadius * cosMargin - sinRadius * sinMargin;
    return dot(direction, center) >= cosLimit;
}