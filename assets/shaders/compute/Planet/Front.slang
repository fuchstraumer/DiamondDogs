implementing PlanetLab;

// Pass 2 of the plate port: where two ocean plates overlap, the plate that more points chose is on top along the
// whole front. Reference: tectonics.OnePlateOnTop. Two dispatches, because the swap needs every vote counted first.
// Both read and write BestPlate, BestCell, SecondPlate and SecondCell from Owners.slang, in place. Each thread
// changes only its own point, so the swap needs no atomic.


bool FindOceanOverlap(in OwnersData owners, out int low, out int high);

[shader("compute")]
[numthreads(64, 1, 1)]
void CountVotesCS(uint3 threadId : SV_DispatchThreadID)
{
    const uint point = threadId.x;
    if (point >= Params.PointCount)
    {
        return;
    }

    int low;
    int high;
    const OwnersData owners = LoadOwnersIn(point);
    if (!FindOceanOverlap(owners, low, high))
    {
        return;
    }

    const uint pair = uint(low) * Params.PlateCount + uint(high);
    PairVotes.Add(pair, 1u);

    if (owners.GetBestPlate() == low)
    {
        LowVotes.Add(pair, 1u);
    }
}

[shader("compute")]
[numthreads(64, 1, 1)]
void SwapToMajorityCS(uint3 threadId : SV_DispatchThreadID)
{
    const uint point = threadId.x;
    if (point >= Params.PointCount)
    {
        return;
    }

    const OwnersData owners = LoadOwners(point);
    int low;
    int high;
    if (!FindOceanOverlap(owners, low, high))
    {
        return;
    }

    const uint pair = uint(low) * Params.PlateCount + uint(high);
    const bool lowWins = 2u * LowVotesIn.Load(pair) >= PairVotesIn.Load(pair);
    const bool lowIsOnTop = owners.GetBestPlate() == low;
    if (lowIsOnTop == lowWins)
    {
        return;
    }

    // if we got this far, second is now best and best is now second, so we swap them
    // swizzle is the best way to handle this without potentially making another intermediary
    StoreOwners(point, owners.Data.zw, owners.Data.xy);
}

bool FindOceanOverlap(in OwnersData owners, out int low, out int high)
{
    low = -1;
    high = -1;

    const int bestPlate = owners.GetBestPlate();
    const int secondPlate = owners.GetSecondPlate();
    if (secondPlate < 0)
    {
        return false;
    }

    const uint bestFlat = uint(bestPlate) * Params.PointCount + uint(owners.GetBestCell());
    const CrustFlagBits crustFlagsBest = LoadCrustFlagsIn(bestFlat);
    const uint secondFlat = uint(secondPlate) * Params.PointCount + uint(owners.GetSecondCell());
    const CrustFlagBits crustFlagsSecond = LoadCrustFlagsIn(secondFlat);
    if (crustFlagsBest.IsContinental() || crustFlagsSecond.IsContinental())
    {
        return false;
    }

    low = min(bestPlate, secondPlate);
    high = max(bestPlate, secondPlate);
    return true;
}
