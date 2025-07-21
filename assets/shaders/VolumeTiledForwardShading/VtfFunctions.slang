implementing VolumeTiledForwardShading;

uint3 GridIdxToGridCoordinate(in const uint idx, uniform uint3 grid_dimensions)
{
    uint x = idx % grid_dimensions.x;
    uint y = idx % (grid_dimensions.x * grid_dimensions.y) / grid_dimensions.x;
    uint z = idx / (grid_dimensions.x * grid_dimensions.y);
    return uint3(x, y, z);
}

uint GridCoordinateToGridIdx(in const uint3 grid_coordinate, uniform uint3 grid_dimensions)
{
    return grid_coordinate.x + (grid_dimensions.x * (grid_coordinate.y + (grid_dimensions.y * grid_coordinate.z)));
}

uint3 ComputeClusterIndex3D(in const float2 screen_pos, in const float view_z, uniform float2 screen_size, uniform float view_near, uniform float log_grid_dim_y)
{
    uint i = uint(screen_pos.x / screen_size.x);
    uint j = uint(screen_pos.y / screen_size.y);
    uint k = uint(log(-view_z / view_near) * log_grid_dim_y);
    return uint3(i, j, k);
}


