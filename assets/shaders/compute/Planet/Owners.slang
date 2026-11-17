implementing PlanetLab;
__include Fibonacci;

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

float3x3 LoadRotation(uint plate);

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

    // the plate sequence decides ties: with equal scores the lower plate stays on top
    for (uint plate = 0; plate < Params.PlateCount; ++plate)
    {
        // row vector times matrix is transpose(R) * w, the world to plate frame
        const float3 frameDirection = mul(worldPoint, LoadRotation(plate));
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

float3x3 LoadRotation(uint plate)
{
    const uint base = plate * 9u;
    return float3x3(
        Rotation.Load(base + 0u), Rotation.Load(base + 1u), Rotation.Load(base + 2u),
        Rotation.Load(base + 3u), Rotation.Load(base + 4u), Rotation.Load(base + 5u),
        Rotation.Load(base + 6u), Rotation.Load(base + 7u), Rotation.Load(base + 8u));
}
