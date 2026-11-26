implementing PlanetLab;
__include Buffers;
__include HEALPix;

// Analytical + live-parameterizable mantle plume & hotspot volcanism pass.
// Grounded in Arnould, Coltice & Flament (EPSL 2020): upper-mantle conduit r_p ~ 80 km,
// dynamic lithospheric swell W_s ~ 1000 km (500-2000 m peak), buoyancy flux ~ 5e3 kg/s,
// and slow mantle-frame conduit drift (~10-50 km/My). Episodic magma pulses, plate-frame
// volcanic rift-arm noise, and two-stage island -> coral atoll -> submerged seamount subsidence
// trail discrete volcanic island chains over rotating lithospheric plates.

static const uint MaxMantlePlumes = 64u;
static const uint PlumeFlagEnabled = 1u << 0u;
static const uint PlumeFlagDriftEnabled = 1u << 1u;

struct MantlePlumeGpu
{
    float4 PositionAndRadius; // xyz = unit mantle-frame position, w = ConduitRadiusKm
    float4 DriftAndSpeed;     // xyz = unit drift rotation axis, w = DriftSpeedKmPerMy
    float4 SwellAndFlux;      // x = SwellRadiusKm, y = PeakSwellKm, z = BuoyancyFlux, w = VolcanicUpliftRate
    float4 TimingAndPulse;    // x = BirthTimeMy, y = LifetimeMy (0 = infinite), z = PulsePeriodMy, w = PulseAmplitude
};

struct PlumeParams
{
    uint PlumeCount;
    float Time;
    float GlobalStrength;
    float OceanicErosionRate;
    float SwellScale;
    float AccretionThresholdKm;
    uint Flags;
    uint Pad;
    MantlePlumeGpu Plumes[MaxMantlePlumes];
};

ConstantBuffer<PlumeParams> PlumeConstants;

internal uint PlumeHash(uint value)
{
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    value ^= value >> 16u;
    return value;
}

internal float PlumeHashSigned(uint value)
{
    const uint hashed = PlumeHash(value);
    return (float(hashed & 0x00ffffffu) / 16777216.0f) * 2.0f - 1.0f;
}

internal float3 RotatePlumePosition(in float3 direction, in float3 unitAxis, in float angleRad)
{
    float sinAngle = 0.0f;
    float cosAngle = 1.0f;
    sincos(angleRad, sinAngle, cosAngle);
    return direction * cosAngle +
           cross(unitAxis, direction) * sinAngle +
           unitAxis * (dot(unitAxis, direction) * (1.0f - cosAngle));
}

internal void PlumeTangentBasis(in float3 unitPos, out float3 tangentEast, out float3 tangentNorth)
{
    const float3 referenceUp = (abs(unitPos.z) < 0.9f) ? float3(0.0f, 0.0f, 1.0f) : float3(1.0f, 0.0f, 0.0f);
    tangentEast = normalize(cross(referenceUp, unitPos));
    tangentNorth = cross(unitPos, tangentEast);
}

internal float EvaluatePlumeLifeEnvelope(in float timeMy, in float birthTimeMy, in float lifetimeMy)
{
    if (timeMy < birthTimeMy)
    {
        return 0.0f;
    }

    if (lifetimeMy <= 0.0f)
    {
        return 1.0f;
    }

    const float elapsed = timeMy - birthTimeMy;
    if (elapsed >= lifetimeMy)
    {
        return 0.0f;
    }

    const float rampIn = min(5.0f, 0.15f * lifetimeMy);
    const float rampOut = min(20.0f, 0.25f * lifetimeMy);
    const float fadeIn = (rampIn > 1.0e-5f) ? smoothstep(0.0f, rampIn, elapsed) : 1.0f;
    const float fadeOut = (rampOut > 1.0e-5f) ? smoothstep(0.0f, rampOut, lifetimeMy - elapsed) : 1.0f;
    return fadeIn * fadeOut;
}

internal float3 EvaluatePlumePosition(in MantlePlumeGpu plume, in float timeMy, in bool driftEnabled)
{
    const float3 originPos = plume.PositionAndRadius.xyz;
    const float driftSpeed = plume.DriftAndSpeed.w;
    if (!driftEnabled || driftSpeed <= 0.0f)
    {
        return originPos;
    }

    const float axisLen = length(plume.DriftAndSpeed.xyz);
    if (axisLen <= 1.0e-5f)
    {
        return originPos;
    }

    const float elapsed = max(0.0f, timeMy - plume.TimingAndPulse.x);
    const float angleRad = elapsed * (driftSpeed / PlanetRadiusKm);
    return normalize(RotatePlumePosition(originPos, plume.DriftAndSpeed.xyz / axisLen, angleRad));
}

internal float GeodesicKm(in float3 unitA, in float3 unitB)
{
    const float chord = length(unitA - unitB);
    return PlanetRadiusKm * 2.0f * asin(clamp(0.5f * chord, 0.0f, 1.0f));
}

internal float SampleVolcanicShieldNoise(in float3 platePoint, in uint plumeIdx)
{
    Swiss swissRule;
    swissRule.gain = 0.5f;
    swissRule.warpStrength = 0.12f;
    swissRule.compoundingWarp = 0;
    swissRule.warpFalloff = 1;
    swissRule.smoothClamp = 1;

    Fractal<Simplex3, Swiss> field;
    field.rule = swissRule;
    field.frequency = 22.0f;
    field.lacunarity = 2.0f;
    field.octaveCount = 2;
    field.seed = 701u + plumeIdx * 31u;
    field.rotateOctaves = 1;
    return field.eval(platePoint);
}

internal float EvaluatePlumeSwellKm(in float3 worldPoint, in float timeMy)
{
    const bool enabled = (PlumeConstants.Flags & PlumeFlagEnabled) != 0u;
    if (!enabled || PlumeConstants.PlumeCount == 0u || PlumeConstants.SwellScale <= 0.0f)
    {
        return 0.0f;
    }

    const bool driftEnabled = (PlumeConstants.Flags & PlumeFlagDriftEnabled) != 0u;
    const uint activeCount = min(PlumeConstants.PlumeCount, MaxMantlePlumes);
    float totalSwell = 0.0f;

    for (uint plumeIdx = 0u; plumeIdx < activeCount; ++plumeIdx)
    {
        const MantlePlumeGpu plume = PlumeConstants.Plumes[plumeIdx];
        const float swellRadiusKm = plume.SwellAndFlux.x;
        const float peakSwellKm = plume.SwellAndFlux.y;
        if (swellRadiusKm <= 0.0f || peakSwellKm <= 0.0f)
        {
            continue;
        }

        const float lifeEnvelope = EvaluatePlumeLifeEnvelope(timeMy, plume.TimingAndPulse.x, plume.TimingAndPulse.y);
        if (lifeEnvelope <= 0.0f)
        {
            continue;
        }

        const float maxAngleRad = min(3.14159265f, (3.5f * swellRadiusKm) / PlanetRadiusKm);
        const float3 plumePos = EvaluatePlumePosition(plume, timeMy, driftEnabled);
        if (dot(worldPoint, plumePos) < cos(maxAngleRad))
        {
            continue;
        }

        const float distKm = GeodesicKm(worldPoint, plumePos);
        const float ratio = distKm / swellRadiusKm;
        totalSwell += PlumeConstants.SwellScale * peakSwellKm * lifeEnvelope * exp(-0.5f * ratio * ratio);
    }

    return min(totalSwell, 3.5f);
}

[shader("compute")]
[numthreads(64, 1, 1)]
void ApplyMantlePlumesCS(uint3 threadId : SV_DispatchThreadID)
{
    const bool enabled = (PlumeConstants.Flags & PlumeFlagEnabled) != 0u;
    if (!enabled || PlumeConstants.PlumeCount == 0u)
    {
        return;
    }

    const uint idxLimit = PlateLiveOffset.Load(StepParams.PlateCount);
    if (threadId.x >= idxLimit)
    {
        return;
    }

    const uint flattenedPoint = LiveCell.Load(threadId.x);
    const CrustFlagBits crustFlags = LoadCrustFlags(flattenedPoint);
    const bool isContinental = crustFlags.IsContinental();

    const uint plateIdx = flattenedPoint / StepParams.PointCount;
    const uint plateCell = flattenedPoint % StepParams.PointCount;
    const float3x4 plateTransform = LoadRotation(plateIdx);
    const float3 latticePoint = HealpixPixToVec(plateCell, StepParams.HealpixOrder);
    const float3 worldPoint = PlateToWorld(latticePoint, plateTransform);

    const bool driftEnabled = (PlumeConstants.Flags & PlumeFlagDriftEnabled) != 0u;
    const uint activeCount = min(PlumeConstants.PlumeCount, MaxMantlePlumes);
    const float timeMy = PlumeConstants.Time;
    const float crustFactor = isContinental ? 0.45f : 1.0f;

    float totalUpliftRate = 0.0f;

    for (uint plumeIdx = 0u; plumeIdx < activeCount; ++plumeIdx)
    {
        const MantlePlumeGpu plume = PlumeConstants.Plumes[plumeIdx];
        const float conduitRadiusKm = plume.PositionAndRadius.w;
        const float buoyancyFlux = plume.SwellAndFlux.z;
        const float volcanicUpliftRate = plume.SwellAndFlux.w;
        if (conduitRadiusKm <= 0.0f || buoyancyFlux <= 0.0f || volcanicUpliftRate <= 0.0f)
        {
            continue;
        }

        const float lifeEnvelope = EvaluatePlumeLifeEnvelope(timeMy, plume.TimingAndPulse.x, plume.TimingAndPulse.y);
        if (lifeEnvelope <= 0.0f)
        {
            continue;
        }

        float3 plumePos = EvaluatePlumePosition(plume, timeMy, driftEnabled);
        float pulseFactor = 1.0f;

        const float pulsePeriod = plume.TimingAndPulse.z;
        const float pulseAmplitude = clamp(plume.TimingAndPulse.w, 0.0f, 1.0f);
        if (pulsePeriod > 0.0f && pulseAmplitude > 0.0f)
        {
            const float elapsed = max(0.0f, timeMy - plume.TimingAndPulse.x);
            const float phaseOffset = float(PlumeHash(plumeIdx * 3571u + 11u) & 0x0fffu) / 4096.0f;
            const float cycle = elapsed / pulsePeriod + phaseOffset * 0.25f * smoothstep(0.45f, 0.85f, pulseAmplitude);
            const uint episodeIdx = uint(max(0.0f, floor(cycle)));
            const float episodePhase = frac(cycle);

            const uint episodeHash = PlumeHash(plumeIdx * 7919u + episodeIdx * 104729u + 17u);
            const float episodicMix = smoothstep(0.45f, 0.85f, pulseAmplitude);
            const float strengthJitter = lerp(1.0f, 0.65f + 0.35f * (1.0f + PlumeHashSigned(episodeHash ^ 0x9e3779b9u)), episodicMix);

            // Step individual volcanic vents slightly across fracture zones during high-episodicity regimes.
            if (episodicMix > 0.01f)
            {
                float3 tangentEast;
                float3 tangentNorth;
                PlumeTangentBasis(plumePos, tangentEast, tangentNorth);
                const float shiftEastKm = conduitRadiusKm * 0.70f * episodicMix * PlumeHashSigned(episodeHash ^ 0x517cc1b7u);
                const float shiftNorthKm = conduitRadiusKm * 0.70f * episodicMix * PlumeHashSigned(episodeHash ^ 0x27220a95u);
                plumePos = normalize(plumePos +
                                     tangentEast * (shiftEastKm / PlanetRadiusKm) +
                                     tangentNorth * (shiftNorthKm / PlanetRadiusKm));
            }

            const float cosineWave = 0.5f * (1.0f - cos(6.2831853f * episodePhase));
            const float sharpness = 1.0f + 4.5f * pulseAmplitude * pulseAmplitude;
            const float burst = pow(saturate(cosineWave), sharpness);
            const float baseFloor = lerp(1.0f - pulseAmplitude, 0.0f, episodicMix);
            pulseFactor = baseFloor + (1.0f - baseFloor) * burst * strengthJitter;
        }

        const float maxAngleRad = min(3.14159265f, (3.6f * conduitRadiusKm) / PlanetRadiusKm);
        if (dot(worldPoint, plumePos) < cos(maxAngleRad))
        {
            continue;
        }

        const float distKm = GeodesicKm(worldPoint, plumePos);
        const float shieldNoise = SampleVolcanicShieldNoise(latticePoint, plumeIdx);
        const float radiusWarp = (pulseAmplitude > 0.45f)
            ? (0.78f + 0.38f * (0.5f - 0.5f * shieldNoise))
            : 1.0f;
        const float warpedDistKm = distKm * radiusWarp;
        const float ratio = warpedDistKm / conduitRadiusKm;
        const float falloffCoeff = (pulseAmplitude > 0.45f) ? 0.85f : 0.5f;
        const float meltProfile = exp(-falloffCoeff * ratio * ratio);
        const float fluxFactor = max(buoyancyFlux, 0.0f) / 5.0f;

        totalUpliftRate += volcanicUpliftRate * fluxFactor * meltProfile * pulseFactor *
                           lifeEnvelope * crustFactor * PlumeConstants.GlobalStrength;
    }

    CrustStateData crustState = LoadCrustState(flattenedPoint);
    float relief = crustState.GetRelief();
    const float dt = StepParams.TimeStepMy;
    bool modified = false;

    if (totalUpliftRate > 0.0f)
    {
        relief += totalUpliftRate * dt;
        modified = true;

        if (totalUpliftRate > 0.05f)
        {
            crustState.SetOrogenyType(OrogenyVolcanic);
            crustState.SetOrogenyTime(timeMy);
        }
    }

    // Two-stage oceanic edifice evolution once clear of the active vent:
    // tall basalt volcanoes bevel rapidly through wave erosion, linger near sea level as coral atolls,
    // and then subside slowly down to submerged seamounts/guyots.
    if (!isContinental &&
        crustState.GetOrogenyType() == OrogenyVolcanic &&
        totalUpliftRate < 0.05f &&
        relief > 0.0f &&
        PlumeConstants.OceanicErosionRate > 0.0f)
    {
        const float oceanAge = max(timeMy - crustState.GetBirth(), 0.0f);
        const float baseOceanElev = max(-(2.6f + 0.345f * sqrt(oceanAge)), -6.0f);
        const float currentElev = baseOceanElev + relief;

        float erosionMultiplier = 1.0f;
        if (currentElev > 0.15f)
        {
            erosionMultiplier = 4.5f;
        }
        else if (currentElev >= -0.35f)
        {
            erosionMultiplier = 0.65f;
        }

        relief = max(0.0f, relief - PlumeConstants.OceanicErosionRate * erosionMultiplier * dt);
        modified = true;
    }

    if (!modified)
    {
        return;
    }

    const float reliefCap = isContinental ? PeakElevation : min(PeakElevation, 8.5f);
    relief = min(relief, reliefCap);

    if (!isContinental &&
        PlumeConstants.AccretionThresholdKm > 0.0f &&
        relief > PlumeConstants.AccretionThresholdKm)
    {
        StoreCrustFlags(flattenedPoint, PackCrustFlags(crustFlags.IsAlive(), true));
    }

    crustState.SetRelief(relief);
    StoreCrustState(flattenedPoint, crustState);
}
