implementing PlanetLab;

// Pass 3 of the plate port: new ocean crust at ridges, removal of crust that subducts, and the four front values
// that passes 4 to 6 read. Reference: tectonics.Step, first half. Two dispatches: the removal and the front values
// read the crust flags after the gap writes of the first dispatch. The crust flags and CrustBirth are the arrays
// that passes 1 and 2 read (Buffers.slang).

struct Pass3Params
{
    float Time;
};

ConstantBuffer<Pass3Params> Pass3Constants;

FloatsIn Poles;
FloatsIn Rates;

Floats CrustRelief;
Ints CrustOrogeny;
Floats CrustOrogenyTime;

Ints Pass3Plate;
Ints Pass3Cell;

Words FrontSubduction;
Floats FrontStrength;
Floats FrontSpeed;
Floats FrontSlabAge;

static const float PlanetRadiusKm = 6370.0f;
static const float MaximumPlateSpeed = 100.0f;
static const float MantleVigor = 1.0f;
static const float TrenchElevation = -10.0f;
static const float PeakElevation = 10.0f;

float3 LoadPole(uint plate);
void WriteNewOceanCrust(uint flat);
void WriteFrontValues(uint point, float3 worldPoint, int topPlate, int underPlate, uint underFlat);

[shader("compute")]
[numthreads(64, 1, 1)]
void FillGapsCS(uint3 threadId : SV_DispatchThreadID)
{
    const uint point = threadId.x;
    if (point >= Params.PointCount)
    {
        return;
    }

    int plate = BestPlate.Load(point);
    int cell = BestCell.Load(point);

    if (plate == -1)
    {
        const int count = int(Params.PointCount);
        float3 worldPoint = LatticePoint(int(point), count);
        plate = PreviousPlate.Load(point);
        float3x4 rotation = LoadRotation(uint(plate));
        worldPoint = WorldToPlate(worldPoint, rotation);
        cell = FibonacciNearest(worldPoint, count);
        WriteNewOceanCrust(uint(plate) * Params.PointCount + uint(cell));
    }

    Pass3Plate.Store(point, plate);
    Pass3Cell.Store(point, cell);
}

[shader("compute")]
[numthreads(64, 1, 1)]
void RemoveSubductedCS(uint3 threadId : SV_DispatchThreadID)
{
    const uint point = threadId.x;
    if (point >= Params.PointCount)
    {
        return;
    }

    FrontSubduction.Store(point, 0u);
    FrontStrength.Store(point, 0.0f);
    FrontSpeed.Store(point, 0.0f);
    FrontSlabAge.Store(point, 0.0f);

    const int underPlate = SecondPlate.Load(point);
    if (underPlate < 0)
    {
        return;
    }

    const int topPlate = Pass3Plate.Load(point);
    const uint topFlat = uint(topPlate) * Params.PointCount + uint(Pass3Cell.Load(point));
    const uint underFlat = uint(underPlate) * Params.PointCount + uint(SecondCell.Load(point));
    const bool collision = LoadContinental(topFlat) != 0u && LoadContinental(underFlat) != 0u;

    WriteFrontValues(point, LatticePoint(int(point), int(Params.PointCount)), topPlate, underPlate, underFlat);

    if (collision)
    {
        return;
    }

    FrontSubduction.Store(point, 1u);
    StoreAlive(underFlat, 0u);
}

float3 LoadPole(uint plate)
{
    const uint base = plate * 3u;
    return float3(Poles.Load(base), Poles.Load(base + 1u), Poles.Load(base + 2u));
}

void WriteNewOceanCrust(uint flat)
{
    StoreAlive(flat, 1u);
    StoreContinental(flat, 0u);
    CrustBirth.Store(flat, Pass3Constants.Time);
    CrustRelief.Store(flat, 0.0f);
    CrustOrogeny.Store(flat, 0);
    CrustOrogenyTime.Store(flat, Pass3Constants.Time);
}

void WriteFrontValues(uint point, float3 worldPoint, int topPlate, int underPlate, uint underFlat)
{
    const float age = Pass3Constants.Time - CrustBirth.Load(underFlat);
    float base = 0.5f;
    if (LoadContinental(underFlat) == 0u)
    {
        base = max(-(2.6f + 0.345f * sqrt(age)), -6.0f);
    }

    const float height = (base + CrustRelief.Load(underFlat) - TrenchElevation) / (PeakElevation - TrenchElevation);
    const float transferBase = clamp(height, 0.0f, 1.0f);
    const float transfer = transferBase * transferBase;

    const float3 spin = LoadPole(uint(topPlate)) * Rates.Load(uint(topPlate)) - LoadPole(uint(underPlate)) * Rates.Load(uint(underPlate));
    const float speed = length(cross(spin, worldPoint)) * PlanetRadiusKm;
    const float strength = clamp(speed / MaximumPlateSpeed, 0.0f, MantleVigor) * transfer;

    FrontSlabAge.Store(point, age);
    FrontSpeed.Store(point, speed);
    FrontStrength.Store(point, strength);
}
