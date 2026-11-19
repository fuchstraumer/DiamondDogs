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

static const float PlanetRadiusKm = 6370.0f;
static const float MaximumPlateSpeed = 100.0f;
static const float MantleVigor = 1.0f;
static const float TrenchElevation = -10.0f;
static const float PeakElevation = 10.0f;

float4 LoadPoleRates(uint plate);
void WriteNewOceanCrust(uint flat);
FrontState ComputeFrontValues(in CrustFlagBits crustFlagsUnder,
                              in uint point,
                              in float3 worldPoint,
                              in int topPlate,
                              in int underPlate,
                              in uint underFlat,
                              in uint subduction);

[shader("compute")]
[numthreads(64, 1, 1)]
void FillGapsCS(uint3 threadId : SV_DispatchThreadID)
{
    const uint point = threadId.x;
    if (point >= Params.PointCount)
    {
        return;
    }

    const OwnersData owners = LoadOwners(point);
    int plate = owners.GetBestPlate();
    int cell = owners.GetBestCell();

    if (plate == -1)
    {
        const int count = int(Params.PointCount);
        float3 worldPoint = LatticePoint(int(point), count);
        plate = PreviousPlate.Load(point);
        float3x4 rotation = LoadRotation(uint(plate));
        worldPoint = WorldToPlate(worldPoint, rotation);
        cell = FibonacciNearest(worldPoint, count);
        WriteNewOceanCrust(uint(plate) * Params.PointCount + uint(cell));
        StoreOwners(point, int2(plate, cell), owners.Data.zw);
    }
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

    FrontState frontState = ZeroFrontState();


    const OwnersData owners = LoadOwnersIn(point);
    const int underPlate = owners.GetSecondPlate();
    if (underPlate >= 0)
    {
        const int topPlate = owners.GetBestPlate();
        const uint topFlat = uint(topPlate) * Params.PointCount + uint(owners.GetBestCell());
        const uint underFlat = uint(underPlate) * Params.PointCount + uint(owners.GetSecondCell());
        const CrustFlagBits crustFlagsTop = LoadCrustFlags(topFlat);
        const CrustFlagBits crustFlagsUnder = LoadCrustFlags(underFlat);
        const bool collision = crustFlagsTop.IsContinental() && crustFlagsUnder.IsContinental();
        const float3 worldPoint = LatticePoint(int(point), int(Params.PointCount));
        frontState = ComputeFrontValues(crustFlagsUnder,
                                        point,
                                        worldPoint,
                                        topPlate,
                                        underPlate,
                                        underFlat,
                                        collision ? 0u : 1u);

        if (!collision)
        {
            StoreCrustFlags(underFlat, PackCrustFlags(false, crustFlagsUnder.IsContinental()));
        }
    }

    StoreFrontState(point, frontState);
}

float4 LoadPoleRates(uint plate)
{
    return PoleRatesIn.Load(plate);
}

void WriteNewOceanCrust(uint flat)
{
    const uint newFlagBits = PackCrustFlags(true, false);
    StoreCrustFlags(flat, newFlagBits);
    StoreCrustState(flat, 0.0f, Pass3Constants.Time, Pass3Constants.Time, 0);
}

FrontState ComputeFrontValues(in CrustFlagBits crustFlagsUnder,
                              in uint point,
                              in float3 worldPoint,
                              in int topPlate,
                              in int underPlate,
                              in uint underFlat,
                              in uint subduction)
{
    const CrustStateData crustState = LoadCrustStateIn(underFlat);
    const float age = Pass3Constants.Time - crustState.GetBirth();
    float base = 0.5f;
    if (!crustFlagsUnder.IsContinental())
    {
        base = max(-(2.6f + 0.345f * sqrt(age)), -6.0f);
    }

    const float height = (base + crustState.GetRelief() - TrenchElevation) / (PeakElevation - TrenchElevation);
    const float transferBase = clamp(height, 0.0f, 1.0f);
    const float transfer = transferBase * transferBase;
    const float4 poleRatesTop = LoadPoleRates(uint(topPlate));
    const float4 poleRatesUnder = LoadPoleRates(uint(underPlate));
    const float3 spin = (poleRatesTop.xyz * poleRatesTop.w) - (poleRatesUnder.xyz * poleRatesUnder.w);
    const float speed = length(cross(spin, worldPoint)) * PlanetRadiusKm;
    const float strength = clamp(speed / MaximumPlateSpeed, 0.0f, MantleVigor) * transfer;
    return { float4(strength, speed, age, asfloat(subduction)) };
}
