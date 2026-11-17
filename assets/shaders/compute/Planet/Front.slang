implementing PlanetLab;

// Pass 2 of the plate port: where two ocean plates overlap, the plate that more points chose is on top along the
// whole front. Reference: tectonics.OnePlateOnTop. Two dispatches, because the swap needs every vote counted first.
// Both read and write BestPlate, BestCell, SecondPlate and SecondCell from Owners.slang, in place. Each thread
// changes only its own point, so the swap needs no atomic.

// P * P integer counts, indexed by low * PlateCount + high. The host clears both to zero before the first dispatch.
// Each vote is one plain atomic add, not AtomicAddWaveAggregated: the pair changes from lane to lane, and about one
// lane in a wave votes (808 of 100 000 points in the reference capture).
Counters PairVotes;
Counters LowVotes;

bool FindOceanOverlap(uint point, out int low, out int high);

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
    if (!FindOceanOverlap(point, low, high))
    {
        return;
    }

    const uint pair = uint(low) * Params.PlateCount + uint(high);
    PairVotes.Add(pair, 1u);

    if (BestPlate.Load(point) == low)
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

    int low;
    int high;
    if (!FindOceanOverlap(point, low, high))
    {
        return;
    }

    const uint pair = uint(low) * Params.PlateCount + uint(high);
    const bool lowWins = 2u * LowVotes.Load(pair) >= PairVotes.Load(pair);
    const bool lowIsOnTop = BestPlate.Load(point) == low;
    if (lowIsOnTop == lowWins)
    {
        return;
    }

    const int plate = BestPlate.Load(point);
    const int cell = BestCell.Load(point);
    BestPlate.Store(point, SecondPlate.Load(point));
    BestCell.Store(point, SecondCell.Load(point));
    SecondPlate.Store(point, plate);
    SecondCell.Store(point, cell);
}

bool FindOceanOverlap(uint point, out int low, out int high)
{
    low = -1;
    high = -1;

    const int bestPlate = BestPlate.Load(point);
    const int secondPlate = SecondPlate.Load(point);
    if (secondPlate < 0)
    {
        return false;
    }

    const uint bestFlat = uint(bestPlate) * Params.PointCount + uint(BestCell.Load(point));
    const uint secondFlat = uint(secondPlate) * Params.PointCount + uint(SecondCell.Load(point));
    if (LoadContinental(bestFlat) != 0u || LoadContinental(secondFlat) != 0u)
    {
        return false;
    }

    low = min(bestPlate, secondPlate);
    high = max(bestPlate, secondPlate);
    return true;
}
