implementing PlanetLab;
__include Buffers;
__include HEALPix;

// The landscape budget of one step: the first, cheap stand-in for erosion. It has two terms, both on land only (the
// part of the crust above sea level):
//
//   Diffusion: h' = h + kappa * sum over the 4 edge neighbours (h_n - h), where h = max(elevation, 0) and
//     kappa = D dt / dx^2. Mountains widen into foothills and lose height; land next to the sea wears down toward sea
//     level, and the sediment leaves the model (no shelf deposit yet). Neighbours on another plate give no flux.
//   Orogenic collapse: elevation above CollapseElevationKm relaxes toward it with time constant CollapseTimeMy. Thick
//     crust flows sideways under its own weight; this is why real plateaus stall near 5 km (Tibet, the Altiplano).
//
// Two dispatches, so that every cell reads the relief of the step before: ErodeReliefCS writes the new relief of each
// live cell to ErodedRelief (by live-list slot), and ApplyErosionCS copies it into CrustState. Volcanic cells
// (OrogenyVolcanic) keep the erosion of Plumes.slang and are not changed here, but they still act as neighbours.
// The stream power law, sediment and isostasy are the next step (see the README).

static const uint ShapingFlagErosionEnabled = 1u << 4u;
static const float ErosionKappaLimit = 0.24f;      // explicit 4-neighbour diffusion is stable below 0.25
static const float ErosionUnchanged = -3.402823466e+38f;

// the new relief of each live cell, by its slot in LiveCell, or ErosionUnchanged
Floats ErodedRelief;
[ls_resource_tags("Aliased:ErodedRelief")]
FloatsIn ErodedReliefIn;

internal float CellElevationKm(in CrustFlagBits flags, in CrustStateData state, in float timeMy)
{
    float base = 0.5f;

    if (!flags.IsContinental())
    {
        const float age = max(timeMy - state.GetBirth(), 0.0f);
        base = max(-(2.6f + 0.345f * sqrt(age)), -6.0f);
    }

    return base + state.GetRelief();
}

[shader("compute")]
[numthreads(64, 1, 1)]
void ErodeReliefCS(uint3 threadId : SV_DispatchThreadID)
{
    const uint slot = threadId.x;

    if (slot >= PlateLiveOffset.Load(StepParams.PlateCount))
    {
        return;
    }

    ErodedRelief.Store(slot, ErosionUnchanged);

    if ((ShapingConstants.Flags & ShapingFlagErosionEnabled) == 0u)
    {
        return;
    }

    const uint pointCount = StepParams.PointCount;
    const uint flattened = LiveCell.Load(slot);
    const CrustFlagBits flags = LoadCrustFlagsIn(flattened);
    const CrustStateData state = LoadCrustStateIn(flattened);
    const float timeMy = UpliftConstants.Time;
    const float elevation = CellElevationKm(flags, state, timeMy);

    if (elevation <= 0.0f || state.GetOrogenyType() == OrogenyVolcanic)
    {
        return;
    }

    // the cell spacing of the HEALPix grid, and the diffusion number of this step
    const float spacingKm = PlanetRadiusKm * sqrt(4.0f * 3.14159265f / float(pointCount));
    const float dt = StepParams.TimeStepMy;
    const float kappa = min(ShapingConstants.ErosionDiffusivity * dt / (spacingKm * spacingKm), ErosionKappaLimit);

    const uint plate = flattened / pointCount;
    const uint cell = flattened - plate * pointCount;
    uint baseFace;
    int faceX;
    int faceY;
    HealpixDecode(cell, StepParams.HealpixOrder, baseFace, faceX, faceY);

    float flux = 0.0f;

    [unroll]
    for (uint cardinal = 0u; cardinal < 4u; ++cardinal)
    {
        const uint direction = cardinal * 2u;
        const int neighbour = HealpixNeighbor(baseFace, faceX, faceY,
                                              k_NeighborDeltaX[direction], k_NeighborDeltaY[direction],
                                              StepParams.HealpixOrder);

        if (neighbour < 0)
        {
            continue;
        }

        const uint neighbourFlat = plate * pointCount + uint(neighbour);
        const CrustFlagBits neighbourFlags = LoadCrustFlagsIn(neighbourFlat);

        // the edge of the plate: the crust on the far side belongs to another plate, so no flux
        if (!neighbourFlags.IsAlive())
        {
            continue;
        }

        const float neighbourHeight = max(CellElevationKm(neighbourFlags, LoadCrustStateIn(neighbourFlat), timeMy), 0.0f);
        flux += neighbourHeight - elevation;
    }

    float change = kappa * flux;

    // orogenic collapse of the part above the collapse elevation
    const float excess = elevation + change - ShapingConstants.CollapseElevationKm;

    if (excess > 0.0f && ShapingConstants.CollapseTimeMy > 0.0f)
    {
        change -= excess * (1.0f - exp(-dt / ShapingConstants.CollapseTimeMy));
    }

    // land wears down to sea level at most: erosion does not dig below the sea
    change = max(change, -elevation);
    ErodedRelief.Store(slot, state.GetRelief() + change);
}

[shader("compute")]
[numthreads(64, 1, 1)]
void ApplyErosionCS(uint3 threadId : SV_DispatchThreadID)
{
    const uint slot = threadId.x;

    if (slot >= PlateLiveOffset.Load(StepParams.PlateCount))
    {
        return;
    }

    const float relief = ErodedReliefIn.Load(slot);

    if (relief == ErosionUnchanged)
    {
        return;
    }

    const uint flattened = LiveCell.Load(slot);
    CrustStateData state = LoadCrustState(flattened);
    state.SetRelief(relief);
    StoreCrustState(flattened, state);
}
