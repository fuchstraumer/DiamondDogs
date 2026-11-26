implementing PlanetLab;
__include Buffers;

struct UpliftParams
{
    float Time;
};

static const uint ShapingFlagSlabDipEnabled      = 1u << 0u;
static const uint ShapingFlagCoastalNoiseEnabled = 1u << 1u;
static const uint ShapingFlagAtollShapingEnabled = 1u << 2u;
static const uint ShapingFlagCustomProfileLut    = 1u << 3u;

struct ShapingParams
{
    float BaseUplift;
    float ContinentErosion;
    float AccretionRelief;
    float AccretionStartRelief;

    float ContinentalReachKm;
    float IslandArcReachKm;
    float ArcDistanceKm;
    float CollisionReachKm;

    float CollisionUpliftScale;
    float SlabDipAgeWeight;
    float SlabDipSpeedWeight;
    float TrenchDepthKm;

    float CoastalWarpAmplitude;
    float CoastalDetailKm;
    float AtollLagoonStrength;
    float NoiseFrequency;

    uint Flags;
    uint NoiseSeed;
    float AtollLandBias;
    float Pad0;

    float4 ProfileLut[4];
};

ConstantBuffer<UpliftParams> UpliftConstants;
ConstantBuffer<ShapingParams> ShapingConstants;

// distances in km, rates are in km per million years (My)
static const float TimeStepMy = 2.0f;
static const float ActiveUplift = 0.002f;
static const int OrogenyAndean = 1;
static const int OrogenyIslandArc = 3;

internal float EvaluateShapingProfileLut(in float u)
{
    const float clamped = saturate(u) * 15.0f;
    const uint idx0 = min(uint(floor(clamped)), 15u);
    const uint idx1 = min(idx0 + 1u, 15u);
    const float fracT = clamped - float(idx0);
    const float v0 = ShapingConstants.ProfileLut[idx0 >> 2u][idx0 & 3u];
    const float v1 = ShapingConstants.ProfileLut[idx1 >> 2u][idx1 & 3u];
    return saturate(lerp(v0, v1, fracT));
}

internal void ComputeEffectiveSubductionReach(in float baseReach,
                                              in float slabAge,
                                              in float speed,
                                              out float effectiveReach,
                                              out float effectivePeakKm)
{
    if ((ShapingConstants.Flags & ShapingFlagSlabDipEnabled) == 0u)
    {
        effectiveReach = baseReach;
        effectivePeakKm = min(ShapingConstants.ArcDistanceKm, 0.4f * baseReach);
        return;
    }

    // Cortial et al. (2019) + Benioff slab dip approximation: volcanic arc forms near ~100 km slab depth,
    // so trench-to-arc distance ~ 100 km / tan(dip). Older slabs sink steeper; faster convergence flattens dip.
    const float ageNorm = clamp((slabAge - 60.0f) / 60.0f, -1.0f, 1.5f);
    const float speedNorm = clamp((speed - 50.0f) / 50.0f, -1.0f, 1.5f);
    const float dipDeg = clamp(
        30.0f + 18.0f * ShapingConstants.SlabDipAgeWeight * ageNorm - 14.0f * ShapingConstants.SlabDipSpeedWeight * speedNorm,
        8.0f, 65.0f);
    const float dipRad = dipDeg * (3.14159265f / 180.0f);
    const float dipArcKm = 100.0f / max(tan(dipRad), 0.12f);
    const float reachScale = clamp(dipArcKm / 173.205f, 0.55f, 1.45f);

    effectiveReach = clamp(baseReach * reachScale, 180.0f, 2200.0f);
    effectivePeakKm = clamp(dipArcKm, 75.0f, 0.45f * effectiveReach);
}

internal float SubductionProfile(float distance, float reach, float peak)
{
    if (distance >= reach || reach <= 1.0e-3f)
    {
        return 0.0f;
    }

    if ((ShapingConstants.Flags & ShapingFlagCustomProfileLut) != 0u)
    {
        return EvaluateShapingProfileLut(distance / reach);
    }

    if (distance < peak)
    {
        return smoothstep(0.0f, 1.0f, distance / max(peak, 1.0f));
    }

    const float scalar = (distance - peak) / max(reach - peak, 1.0f);
    return 1.0f - smoothstep(0.0f, 1.0f, scalar);
}

[shader("compute")]
[numthreads(64, 1, 1)]
void UpliftCS(uint3 threadID : SV_DispatchThreadID)
{
    const uint pointLimit = PlateLiveOffset.Load(StepParams.PlateCount);
    if (threadID.x >= pointLimit)
    {
        return;
    }

    const uint flattenedPt = LiveCell.Load(threadID.x);
    const CrustFlagBits crustFlags = LoadCrustFlags(flattenedPt);
    const bool isContinental = crustFlags.IsContinental();
    CrustStateData crustState = ZeroCrustState();
    float relief = 0.0f;

    const CellState cellState = LoadCellState(flattenedPt);
    const int frontPoint = cellState.GetFrontPoint();
    if (frontPoint >= 0)
    {
        const FrontState frontState = LoadFrontStateIn(frontPoint);
        const float baseReach = isContinental ? ShapingConstants.ContinentalReachKm : ShapingConstants.IslandArcReachKm;
        float reach = baseReach;
        float peakKm = min(ShapingConstants.ArcDistanceKm, 0.4f * baseReach);
        ComputeEffectiveSubductionReach(baseReach, frontState.GetSlabAge(), frontState.GetSpeed(), reach, peakKm);

        const float shape = SubductionProfile(cellState.GetDistance(), reach, peakKm);
        const float rate = ShapingConstants.BaseUplift * shape * frontState.GetStrength();
        const float dt = StepParams.TimeStepMy;
        crustState = LoadCrustState(flattenedPt);
        relief = crustState.GetRelief() + (rate * dt);

        if (rate > ActiveUplift)
        {
            const int updatedOrogeny = isContinental ? OrogenyAndean : OrogenyIslandArc;
            crustState.SetOrogenyType(updatedOrogeny);
            crustState.SetOrogenyTime(UpliftConstants.Time);
        }
    }

    if (isContinental)
    {
        relief *= 1.0f - ShapingConstants.ContinentErosion * StepParams.TimeStepMy / PeakElevation;
    }

    relief = min(relief, PeakElevation);

    // a tall ocean island arc is upgraded to a continent at this point (volcanic hotspot chains stay oceanic)
    if (!isContinental && crustState.GetOrogenyType() != OrogenyVolcanic && (relief > ShapingConstants.AccretionRelief))
    {
        uint flagBits = PackCrustFlags(crustFlags.IsAlive(), true);
        StoreCrustFlags(flattenedPt, flagBits);
        relief = ShapingConstants.AccretionStartRelief;
    }

    if (frontPoint >= 0 || isContinental)
    {
        crustState.SetRelief(relief);
        StoreCrustState(flattenedPt, crustState);
    }
}