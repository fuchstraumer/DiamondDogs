implementing VolumeTiledForwardShading;
__include VtfConfig;

// Cluster grid arithmetic. The cluster grid is uniform on x and y, and exponential on z.

internal uint3 GridIdxToGridCoordinate(in const uint idx, in const uint3 grid_dimensions)
{
    uint x = idx % grid_dimensions.x;
    uint y = idx % (grid_dimensions.x * grid_dimensions.y) / grid_dimensions.x;
    uint z = idx / (grid_dimensions.x * grid_dimensions.y);
    return uint3(x, y, z);
}

internal uint GridCoordinateToGridIdx(in const uint3 grid_coordinate, in const uint3 grid_dimensions)
{
    return grid_coordinate.x + (grid_dimensions.x * (grid_coordinate.y + (grid_dimensions.y * grid_coordinate.z)));
}

// fix: the second argument was named screen_size, and a caller that gave it the size of the whole
// screen got 0 or 1 for i and j. The divisor is the size of one cluster in pixels. The name now states
// that, and ClusterData holds the value under the same name.
internal uint3 ComputeClusterIndex3D(
    in const float2 screen_pos,
    in const float view_z,
    in const float2 cluster_size_in_pixels,
    in const float view_near,
    in const float log_grid_dim_y)
{
    uint i = uint(screen_pos.x / cluster_size_in_pixels.x);
    uint j = uint(screen_pos.y / cluster_size_in_pixels.y);
    // The z slice is exponential, so that each slice covers a constant ratio of depth.
    uint k = uint(log(-view_z / view_near) * log_grid_dim_y);
    return uint3(i, j, k);
}

// We use the following to generate debug cluster colors: we used to use a texel buffer of colors
// we filled once, but a hash gives the same result without needing a buffer, a binding, or an upload.

// PCG integer hash. It gives a well spread result for the small consecutive indices that a cluster
// grid produces, which a multiply-shift hash does not.
internal uint PcgHash(in const uint input)
{
    const uint state = input * 747796405u + 2891336453u;
    const uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

// A distinct colour for each cluster. Neighbouring indices give unrelated colours, so the boundary
// between two clusters is visible.
internal float3 ClusterIndexToDebugColor(in const uint cluster_index)
{
    const uint hash = PcgHash(cluster_index + 1u);

    const float3 raw = float3(
        float((hash >> 0u) & 0xFFu),
        float((hash >> 8u) & 0xFFu),
        float((hash >> 16u) & 0xFFu)) * (1.0f / 255.0f);

    // The raw hash gives dark colours as often as bright ones, and a dark overlay reads as absent
    // rather than as a cluster. The floor keeps every channel visible against a shaded image.
    return lerp(float3(0.25f, 0.25f, 0.25f), float3(1.0f, 1.0f, 1.0f), raw);
}

// The leaf level that holds a given number of items in a tree of the given branching factor. The BVH
// build and the BVH traversal both need this value, and each one held its own table of it.
internal uint ComputeBvhLevelsForItemCount(in const uint item_count, in const uint branching)
{
    uint levels = 1u;
    uint capacity = branching;
    while (capacity < item_count && levels < VTF_BVH_MAX_LEVELS)
    {
        capacity *= branching;
        ++levels;
    }
    return levels;
}

// ---------------------------------------------------------------------------------------------
// BVH level tables. The tree is VTF_BVH_BRANCHING-ary.
//
// fix: VtfBuildBVH held a table of 7 entries and VtfAssignLightsToClustersBVH held a table of 6, and
// the second table was the first one shifted by a level. A tree of depth 7 therefore indexed the
// traversal table out of bounds. One table now serves both, and each file reads the same entry for the
// same level.
//
// VTF_BVH_NUM_LEVEL_NODES[i] is the node count of level i, which is branching to the power i.
// VTF_BVH_FIRST_NODE_INDEX[i] is the index of the first node of level i, which is the sum of every
// earlier level.
// ---------------------------------------------------------------------------------------------

internal static const uint VTF_BVH_NUM_LEVEL_NODES[7] =
{
    1u,
    32u,
    1024u,
    32768u,
    1048576u,
    33554432u,
    1073741824u
};

internal static const uint VTF_BVH_FIRST_NODE_INDEX[7] =
{
    0u,
    1u,
    33u,
    1057u,
    33825u,
    1082401u,
    34636833u
};
