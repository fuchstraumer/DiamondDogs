implementing PlanetLab;
__include Buffers;

struct UpliftParams
{
    float Time;
};

ConstantBuffer<UpliftParams> UpliftConstants;
// this whole table of constants needs to be moved to a uniform buffer as well - 
// for now, as we build this out, we're just copying the exact constants from
// `crust.py` and `tectonics.py` so we can verify against that
// distances in km, rates are in km per million years (My)
static const float TimeStepMy = 2.0f;
static const float BaseUplift = 0.6f;
static const float ArcDistanceKm = 250.0f;
static const float ContinentalReachKm = 1800.0f;
static const float IslandArcReachKm = 450.0f;
static const float ActiveUplift = 0.002f;
static const float ContinentErosion = 0.06f;
static const float AccretionRelief = 1.5f;
static const float AccretionStartRelief = 0.3f;
static const int OrogenyAndean = 1;
static const int OrogenyIslandArc = 3;

// todo: this is one of our biggest levers for art directing and changing the shape
// of the uplifted terrain, as this curve is just ours and is mocked up from a few papers
float SubductionProfile(float distance, float reach)
{
    const float peak = min(ArcDistanceKm, 0.4f * reach);
    if (distance < peak)
    {
        return smoothstep(0.0f, 1.0f, distance / peak);
    }

    const float scalar = (distance - peak) / (reach - peak);
    return 1.0f - smoothstep(0.0f, 1.0f, scalar);
}

[shader("compute")]
[numthreads(64, 1, 1)]
void UpliftCS(uint3 threadID : SV_DispatchThreadID)
{
    const uint pointLimit = PlateLiveOffset.Load(Params.PlateCount);
    if (threadID.x >= pointLimit)
    {
        return;
    }
    
    const uint flattenedPt = LiveCell.Load(threadID.x);
    const CrustFlagBits crustFlags = LoadCrustFlags(flattenedPt);
    const bool isContinental = crustFlags.IsContinental();
    CrustStateData crustState = ZeroCrustState();
    float relief = 0.0f;

    // now use the output of the last pass - a cell with a front point
    // rises by the profile function times the points strength. a fast 
    // uplift will mark this cell as an active mountain range, stamped
    // with the time
    const CellState cellState = LoadCellState(flattenedPt);
    const int frontPoint = cellState.GetFrontPoint();
    if (frontPoint >= 0)
    {
        const float reach = isContinental ? ContinentalReachKm : IslandArcReachKm;
        // note: this is the *cell* distance, it comes from the cell index not frontPoint
        const float shape = SubductionProfile(cellState.GetDistance(), reach);
        const FrontState frontState = LoadFrontStateIn(frontPoint);
        const float rate = BaseUplift * shape * frontState.GetStrength();
        const float dt = Params.TimeStepMy;
        crustState = LoadCrustState(flattenedPt);
        relief = crustState.GetRelief() + (rate * dt);

        if (rate > ActiveUplift)
        {
            const int updatedOrogeny = isContinental ? OrogenyAndean : OrogenyIslandArc;
            crustState.SetOrogenyType(updatedOrogeny);
            crustState.SetOrogenyTime(UpliftConstants.Time);
        }
    }

    // slight erosion: cap, accrete, store
    if (isContinental)
    {
        relief *= 1.0f - ContinentErosion * Params.TimeStepMy / PeakElevation;
    }

    // peak elevation will also need to adjust for gravity: Olympus Mons vs Everest, for example!
    relief = min(relief, PeakElevation);

    // a tall ocean island arc is upgraded to a continent at this point
    if (!isContinental && (relief > AccretionRelief))
    {
        uint flagBits = PackCrustFlags(crustFlags.IsAlive(), true);
        StoreCrustFlags(flattenedPt, flagBits);
        relief = AccretionStartRelief;
    }

    if (frontPoint >= 0 || isContinental)
    {
        crustState.SetRelief(relief);
        StoreCrustState(flattenedPt, crustState);
    }

}