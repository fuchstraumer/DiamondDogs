// Grids map an output element to the position where the root field is evaluated. Plane is the only grid today; the
// cube-face, lat-lon and HEALPix grids follow with the rasterizer of the noise graph.
module NoiseGrids;

/// A rectangle of the plane z = 0. 2D noise in this library is 3D noise at z = 0.
public struct PlaneGrid
{
    /// The position of element (0, 0).
    public float2 origin;
    /// The distance between two elements.
    public float2 spacing;
    /// The element count on each side.
    public int2 extent;

    /// Returns the position of the center of one element.
    public float3 position(int2 element)
    {
        const float2 planar = origin + (float2(element) + 0.5) * spacing;
        return float3(planar, 0.0);
    }

    public bool contains(int2 element)
    {
        return all(element >= int2(0)) && all(element < extent);
    }

    public int linearIndex(int2 element)
    {
        return element.y * extent.x + element.x;
    }
}
