implementing PlanetLab;

// Test entry point for pass 0: one thread maps one direction to its nearest lattice index.
// PlanetGenCheck (applications/PlanetGen) compares the result with map_nearest.bin.

struct MapProbeParams
{
    uint DirectionCount;
    uint LatticeCount;
};

ConstantBuffer<MapProbeParams> MapProbeConstants;

// x, y, z for each direction, flat
FloatsIn MapDirections;
Words MapResults;

[shader("compute")]
[numthreads(64, 1, 1)]
void MapProbeCS(uint3 threadId : SV_DispatchThreadID)
{
    const uint row = threadId.x;
    if (row >= MapProbeConstants.DirectionCount)
    {
        return;
    }

    const uint base = row * 3u;
    const float3 direction = float3(MapDirections.Load(base), MapDirections.Load(base + 1u), MapDirections.Load(base + 2u));
    MapResults.Store(row, uint(FibonacciNearest(direction, int(MapProbeConstants.LatticeCount))));
}
