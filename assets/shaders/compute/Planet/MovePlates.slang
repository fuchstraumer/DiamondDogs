implementing PlanetLab;
__include Buffers;

// Advances plate Euler poles, angular rates, and plate-to-world rotation matrices on the GPU
// from Pass 6 torque sums, then clears all per-plate and per-pair atomic counters for the next tick.
// Reference: tectonics.MovePlates + tectonics.RotationMatrix (applications/PlanetLab/planetlab/tectonics.py).
// Dispatch: 1 workgroup (thread count = 1, or >= PlateCount). Uses no wave ops, so it is wave-size agnostic.

struct MovePlatesParams
{
    float Time;
    float TimeStepMy;
};

ConstantBuffer<MovePlatesParams> MovePlatesConstants;

static const uint MovePlatesGroupSize = 32u;

static const float KeelDrag = 1.5f;
static const float SlabPullSpeed = 0.010f;
static const float PlateDragTime = 10.0f;
static const float SlabFreeSpeed = 10.0f;
static const float RiftSlowSpeed = 5.0f;
static const float RiftFastSpeed = 20.0f;
static const float RiftSlowTime = 20.0f;
static const float RiftPushTime = 100.0f;
static const float PullScaleInv = 1.0f / 4096.0f;

float3x3 RodriguesRotation(in float3 pole, in float angle)
{
    float s, c;
    sincos(angle, s, c);
    const float t = 1.0f - c;

    return float3x3(
        c + t * pole.x * pole.x,          t * pole.x * pole.y - s * pole.z, t * pole.x * pole.z + s * pole.y,
        t * pole.y * pole.x + s * pole.z, c + t * pole.y * pole.y,          t * pole.y * pole.z - s * pole.x,
        t * pole.z * pole.x - s * pole.y, t * pole.z * pole.y + s * pole.x, c + t * pole.z * pole.z
    );
}

float3x3 Reorthogonalize(in float3x3 mat)
{
    const float3 r0 = normalize(mat[0]);
    const float3 r1 = normalize(mat[1] - dot(mat[1], r0) * r0);
    const float3 r2 = cross(r0, r1);
    return float3x3(r0, r1, r2);
}

[shader("compute")]
[numthreads(MovePlatesGroupSize, 1, 1)]
void MovePlatesAndClearCountersCS(uint3 threadId : SV_DispatchThreadID)
{
    const uint plateCount = Params.PlateCount;
    const float dt = MovePlatesConstants.TimeStepMy;

    for (uint plate = threadId.x; plate < plateCount; plate += MovePlatesGroupSize)
    {
        // 1. Read Pass 6 torque sums and plate cell counts
        const uint counterBase = plate * 4u;
        const uint pullBase = plate * 3u;
        const int cellCount = PlateCounters.Load(counterBase + 3u);
        const uint continentalCount = PlateContinentalCount.Load(plate);
        const float3 pull = float3(
            float(PlatePull.Load(pullBase + 0u)),
            float(PlatePull.Load(pullBase + 1u)),
            float(PlatePull.Load(pullBase + 2u))
        ) * PullScaleInv;

        // 2. Torque balance: drive (slab pull + rift push) vs basal + continental keel drag
        const float area = max(float(cellCount), 1.0f);
        const float continentalShare = float(continentalCount) / area;
        const float drag = 1.0f + KeelDrag * continentalShare;

        const float4 plateRift = PlateRiftIn.Load(plate);
        const float riftAge = (MovePlatesConstants.Time - plateRift.w) * MantleVigor;
        const float riftPush = (riftAge < RiftSlowTime)
            ? RiftSlowSpeed
            : ((riftAge < RiftPushTime) ? RiftFastSpeed : 0.0f);

        const float3 balance = (pull * SlabPullSpeed / drag + plateRift.xyz * riftPush) * (MantleVigor / PlanetRadiusKm);
        const float4 poleRate = PoleRatesRW.Load(plate);
        float3 spin = poleRate.xyz * poleRate.w;
        spin += (balance - spin) * min(dt * MantleVigor / PlateDragTime, 1.0f);

        const float rawRate = length(spin);
        const float lowRate = SlabFreeSpeed * MantleVigor / PlanetRadiusKm;
        const float highRate = MaximumPlateSpeed * MantleVigor / PlanetRadiusKm;

        const float3 newPole = (rawRate > 1.0e-12f) ? (spin / rawRate) : poleRate.xyz;
        const float newRate = clamp(rawRate, lowRate, highRate);
        PoleRatesRW.Store(plate, float4(newPole, newRate));

        // 3. Advance plate-to-world rotation matrix: R_next = R(newPole, newRate * dt) * R
        const uint rotBase = plate * 3u;
        const float3x3 currentRot = float3x3(
            RotationsRW.Load(rotBase + 0u).xyz,
            RotationsRW.Load(rotBase + 1u).xyz,
            RotationsRW.Load(rotBase + 2u).xyz
        );
        const float3x3 deltaRot = RodriguesRotation(newPole, newRate * dt);
        const float3x3 nextRot = Reorthogonalize(mul(deltaRot, currentRot));

        RotationsRW.Store(rotBase + 0u, float4(nextRot[0], 0.0f));
        RotationsRW.Store(rotBase + 1u, float4(nextRot[1], 0.0f));
        RotationsRW.Store(rotBase + 2u, float4(nextRot[2], 0.0f));

        // 4. Clear per-plate atomic counters for the next tick
        PlateLiveCursor.Store(plate, 0u);
        PlateFrontPointCount.Store(plate, 0u);
        PlateFrontPointCursor.Store(plate, 0u);
        PlateContinentalCount.Store(plate, 0u);
        PlateCounters.Store(counterBase + 0u, 0);
        PlateCounters.Store(counterBase + 1u, 0);
        PlateCounters.Store(counterBase + 2u, 0);
        PlateCounters.Store(counterBase + 3u, 0);
        PlatePull.Store(pullBase + 0u, 0);
        PlatePull.Store(pullBase + 1u, 0);
        PlatePull.Store(pullBase + 2u, 0);
    }

    // 5. Clear P * P ocean-overlap vote counters for Pass 2 of the next tick
    const uint pairCount = plateCount * plateCount;
    for (uint pair = threadId.x; pair < pairCount; pair += MovePlatesGroupSize)
    {
        PairVotes.Store(pair, 0u);
        LowVotes.Store(pair, 0u);
    }
}
