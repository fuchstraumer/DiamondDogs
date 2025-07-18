layout(early_fragment_tests) in;
#include "Structures.glsl"
#pragma USE_RESOURCES GlobalResources
#pragma USE_RESOURCES VolumetricForward
#pragma USE_RESOURCES Debug

uvec3 IdxToCoord(uint idx) {
    uvec3 result;
    result.x = idx % ClusterData.GridDim.x;
    result.y = idx % (ClusterData.GridDim.x * ClusterData.GridDim.y) / ClusterData.GridDim.x;
    result.z = idx / (ClusterData.GridDim.x * ClusterData.GridDim.y);
    return result;
}

uint CoordToIdx(uvec3 coord) {
    return coord.x + (ClusterData.GridDim.x * (coord.y + ClusterData.GridDim.y * coord.z));
}

uvec3 ComputeClusterIndex3D(vec2 screen_pos, float view_z) {
    uint i = uint(screen_pos.x / ClusterData.ScreenSize.x);
    uint j = uint(screen_pos.y / ClusterData.ScreenSize.y);
    uint k = uint(log(-view_z / ClusterData.ViewNear) * ClusterData.LogGridDimY);
    return uvec3(i, j, k);
}

void main() {
    uvec3 cluster_index_3d = ComputeClusterIndex3D(gl_FragCoord.xy, vPosition.z);
    uint idx = CoordToIdx(cluster_index_3d);
    imageStore(ClusterFlags, int(idx), uvec4(1, 0, 0, 0));
    // cluster color written out is used for debugging.
    backbuffer = imageLoad(ClusterColors, int(idx));
    backbuffer.a = 0.2f;
}
