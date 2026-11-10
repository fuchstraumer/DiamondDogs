// Cellular noise: the distances from a position to the nearest feature points (sites) of a jittered lattice.
//
// The algorithm is Steven Worley's, from "A Cellular Texture Basis Function" (SIGGRAPH 1996): scatter sites in each
// lattice cell, search the cell and its neighbors, and return a function of the nearest distances F1 and F2. This file
// implements it from the paper's description, with these changes: the site count per cell is floor(density + u) with
// u uniform, not Worley's Poisson count; the search covers the 3x3x3 neighborhood only; and the hash is pcg4d
// (NoiseHash). The old CUDA_Noise Voronoi kernel never ran, so nothing here comes from it.
module NoiseCellular;
import NoiseHash;
__exported import NoiseField;
__exported import NoiseLattice;

/// The largest site count of one cell. density is clamped to [1, k_MaxSitesPerCell].
public static const int k_MaxSitesPerCell = 4;

// metric values
public static const int k_CellularEuclidean = 0;
public static const int k_CellularEuclideanSquared = 1;
public static const int k_CellularManhattan = 2;
public static const int k_CellularChebyshev = 3;
public static const int k_CellularMinkowski = 4;

// output values
public static const int k_CellularF1 = 0;
public static const int k_CellularF2 = 1;
public static const int k_CellularF2MinusF1 = 2;
public static const int k_CellularF1PlusF2 = 3;
public static const int k_CellularF1TimesF2 = 4;
public static const int k_CellularCellValue = 5;

// separates the hash streams of the site count and of the sites themselves
static const uint k_CountStream = 0x68E31DA4u;
static const uint k_SiteStream = 0x9E3779B9u;

/// The two nearest sites of a position, relative to the origin of the position's own lattice cell, and the hash id of
/// each. An id depends only on the integer cell, the site index and the seed, so it is the same in every tile.
public struct CellularSites
{
    public float3 nearest;
    public float3 second;
    public uint nearestId;
    public uint secondId;
}

/// Returns the distance of an offset under one metric. Minkowski uses the exponent; the others ignore it.
[Differentiable]
public float CellularDistance(float3 offset, no_diff int metric, no_diff float exponent)
{
    const float3 magnitude = abs(offset);

    switch (metric)
    {
    case k_CellularEuclideanSquared:
        return dot(offset, offset);
    case k_CellularManhattan:
        return magnitude.x + magnitude.y + magnitude.z;
    case k_CellularChebyshev:
        return max(magnitude.x, max(magnitude.y, magnitude.z));
    case k_CellularMinkowski:
    {
        // the floor keeps pow and its derivative finite where one component is exactly 0
        const float3 safe = max(magnitude, float3(1.0e-6));
        const float3 powered = pow(safe, float3(exponent));
        return pow(powered.x + powered.y + powered.z, 1.0 / exponent);
    }
    default:
        return length(offset);
    }
}

public struct Cellular3 : IField3
{
    /// The lattice frequency: cells per unit of position. The cell size is 1 / frequency.
    public float frequency;
    /// How far a site may move from the center of its cell: 0 is a regular grid, 1 fills the whole cell.
    public float jitter;
    /// The mean site count of one cell, in [1, k_MaxSitesPerCell]. The distances are multiplied by cbrt(density), so
    /// density changes the number of cells and not the output range.
    public float density;
    /// One of the k_Cellular metric values.
    public int metric;
    /// The exponent of the Minkowski metric: 1 is Manhattan, 2 is Euclidean, large values approach Chebyshev.
    public float minkowskiExponent;
    /// One of the k_Cellular output values.
    public int output;
    public uint seed;

    /// Finds the two nearest sites of a position in lattice space. Not differentiable: the search only chooses sites.
    public CellularSites findSites(int3 cell, float3 frac)
    {
        const float clampedDensity = clamp(density, 1.0, float(k_MaxSitesPerCell));

        CellularSites sites;
        sites.nearest = float3(0.0);
        sites.second = float3(0.0);
        sites.nearestId = 0u;
        sites.secondId = 0u;
        float nearestDistance = 3.0e38;
        float secondDistance = 3.0e38;

        for (int z = -1; z <= 1; ++z)
        {
            for (int y = -1; y <= 1; ++y)
            {
                for (int x = -1; x <= 1; ++x)
                {
                    const int3 neighbor = cell + int3(x, y, z);
                    const uint countHash = HashCell(neighbor, seed ^ k_CountStream).x;
                    const float countDraw = float(countHash >> 8u) * (1.0 / 16777216.0);
                    const int count = clamp(int(floor(clampedDensity + countDraw)), 1, k_MaxSitesPerCell);

                    for (int site = 0; site < k_MaxSitesPerCell; ++site)
                    {
                        if (site >= count)
                        {
                            break;
                        }

                        const uint4 hashes = HashCell(neighbor, seed + uint(site + 1) * k_SiteStream);
                        const float3 unit = float3(hashes.xyz >> 8u) * (1.0 / 16777216.0);
                        const float3 position = float3(x, y, z) + 0.5 + jitter * (unit - 0.5);
                        const float distance = CellularDistance(position - frac, metric, minkowskiExponent);

                        if (distance < nearestDistance)
                        {
                            secondDistance = nearestDistance;
                            sites.second = sites.nearest;
                            sites.secondId = sites.nearestId;
                            nearestDistance = distance;
                            sites.nearest = position;
                            sites.nearestId = hashes.w;
                        }
                        else if (distance < secondDistance)
                        {
                            secondDistance = distance;
                            sites.second = position;
                            sites.secondId = hashes.w;
                        }
                    }
                }
            }
        }

        return sites;
    }

    /// Returns the output for fixed sites. The search is done; only the distances to the chosen sites carry a
    /// derivative.
    [Differentiable, NoDiffThis]
    public float evalSites(float3 frac, no_diff CellularSites sites)
    {
        const float scale = pow(clamp(density, 1.0, float(k_MaxSitesPerCell)), 1.0 / 3.0);
        const float nearest = CellularDistance(sites.nearest - frac, metric, minkowskiExponent) * scale;
        const float second = CellularDistance(sites.second - frac, metric, minkowskiExponent) * scale;

        switch (output)
        {
        case k_CellularF2:
            return second;
        case k_CellularF2MinusF1:
            return second - nearest;
        case k_CellularF1PlusF2:
            return 0.5 * (nearest + second);
        case k_CellularF1TimesF2:
            return nearest * second;
        case k_CellularCellValue:
            return no_diff HashToSignedUnit(sites.nearestId);
        default:
            return nearest;
        }
    }

    [Differentiable, NoDiffThis]
    public float eval(float3 p)
    {
        const LatticePoint point = MakeLatticePoint(p * frequency);
        const CellularSites sites = no_diff findSites(point.cell, detach(point.frac));
        return evalSites(point.frac, sites);
    }

    /// One search, then three forward-mode passes over the distances only. The default evalGrad would search three
    /// times.
    override public float4 evalGrad(float3 p)
    {
        const LatticePoint point = MakeLatticePoint(p * frequency);
        const CellularSites sites = findSites(point.cell, point.frac);
        const DifferentialPair<float> alongX = fwd_diff(EvalCellularSites)(this, diffPair(point.frac, float3(1.0, 0.0, 0.0)), sites);
        const DifferentialPair<float> alongY = fwd_diff(EvalCellularSites)(this, diffPair(point.frac, float3(0.0, 1.0, 0.0)), sites);
        const DifferentialPair<float> alongZ = fwd_diff(EvalCellularSites)(this, diffPair(point.frac, float3(0.0, 0.0, 1.0)), sites);

        // frac moves by frequency for each unit of p
        return float4(alongX.p, float3(alongX.d, alongY.d, alongZ.d) * frequency);
    }

    /// Returns an upper bound of the output: the largest distance that the 3x3x3 search can return, under the metric.
    public float bound()
    {
        const float scale = pow(clamp(density, 1.0, float(k_MaxSitesPerCell)), 1.0 / 3.0);
        float reach = 2.0 * sqrt(3.0);

        switch (metric)
        {
        case k_CellularEuclideanSquared:
            reach = 12.0;
            break;
        case k_CellularManhattan:
            reach = 6.0;
            break;
        case k_CellularChebyshev:
            reach = 2.0;
            break;
        case k_CellularMinkowski:
            reach = 2.0 * pow(3.0, 1.0 / max(minkowskiExponent, 0.1));
            break;
        default:
            break;
        }

        const float largest = reach * scale;

        switch (output)
        {
        case k_CellularF1TimesF2:
            return largest * largest;
        case k_CellularCellValue:
            return 1.0;
        default:
            return largest;
        }
    }
}

// fwd_diff of a method on a struct value goes through a free function, as in NoiseField
[Differentiable]
float EvalCellularSites(no_diff Cellular3 cellular, float3 frac, no_diff CellularSites sites)
{
    return cellular.evalSites(frac, sites);
}
