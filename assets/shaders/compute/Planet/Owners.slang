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

bool InsideCap(in float3 direction,
               in float3 center,
               in float spread,
               in float cosMargin,
               in float sinMargin);

[shader("compute")]
[numthreads(64, 1, 1)]
void OwnersCS(uint3 threadId : SV_DispatchThreadID)
{
    const uint point = threadId.x;
    if (point >= Params.PointCount)
    {
        return;
    }

    const int count = int(Params.PointCount);
    const float3 worldPoint = LatticePoint(int(point), count);
    const int previousPlate = PreviousPlate.Load(point);

    float bestScore = NoScore;
    float secondScore = NoScore;
    int2 best = int2(-1, -1);
    int2 second = int2(-1, -1);

    const float marginAmount = 2.0f * sqrt(4.0f * 3.141592653589793f / float(count));
    float2 margin;
    // margin.x = sin, margin.y = cos
    sincos(marginAmount, margin.x, margin.y);

    // the plate sequence decides ties: with equal scores the lower plate stays on top
    for (uint plate = 0; plate < Params.PlateCount; ++plate)
    {
        // row vector times matrix is transpose(R) * w, the world to plate frame
        const float3 frameDirection = mul(worldPoint, LoadRotation(plate));

        // check the plate caps, if any have been computed yet, to try and exclude this
        // plate if it's far from the current point
        const float4 plateCap = PlateCapCenters.Load(plate);
        // plateCap.w is zero initialized on CPU, and empty==2.0f. this won't
        // be hit at all during the first tick, as mentioned elsewhere. it's
        // an optimization that occurs after the first tick
        if (plateCap.w == PlateCapEmpty)
        {
            continue;
        }

        const float plateSpread = asfloat(PlateCapSpread.Load(plate));
        const bool isInsideCap = InsideCap(frameDirection, plateCap.xyz, plateSpread, margin.y, margin.x);
        if (plateCap.w == PlateCapValid && !isInsideCap)
        {
            continue; // skip this plate if the point is outside a valid cap
        }

        const int cell = FibonacciNearest(frameDirection, count);
        const uint flat = plate * Params.PointCount + uint(cell);

        if (LoadAlive(flat) == 0u)
        {
            continue;
        }

        float score = CrustBirth.Load(flat);
        if (LoadContinental(flat) != 0u)
        {
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