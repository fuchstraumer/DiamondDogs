
#pragma BEGIN_CUSTOM_INTERFACE
layout (location = 0) out vec4 vMin;
layout (location = 1) out vec4 vMax;
layout (location = 2) out vec4 vColor;
#pragma END_CUSTOM_INTERFACE
#include "Structures.glsl"

#pragma USE_RESOURCES Debug
#pragma USE_RESOURCES VolumetricForward

void main() {
    uint cluster_index = imageLoad(UniqueClusters, int(gl_VertexIndex)).r;
    vMin = ClusterAABBs.Data[cluster_index].Min;
    vMax = ClusterAABBs.Data[cluster_index].Max;
    vec4 out_color = imageLoad(ClusterColors, int(cluster_index));
    vColor = out_color;
}
