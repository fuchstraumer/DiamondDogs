layout (points) in;
layout (triangle_strip, max_vertices = 16) out;
#pragma INTERFACE_OVERRIDE
layout (location = 0) in vec4 vMin[];
layout (location = 1) in vec4 vMax[];
layout (location = 2) in vec4 vColor[];
layout (location = 0) out vec4 gColor;
#pragma END_INTERFACE_OVERRIDE

#pragma USE_RESOURCES Debug

out gl_PerVertex {
    vec4 gl_Position;
};

const vec4 base_colors[8] = {
    vec4( 0.0f, 0.0f, 0.0f, 1.0f ),       // Black
    vec4( 0.0f, 0.0f, 1.0f, 1.0f ),       // Blue
    vec4( 0.0f, 1.0f, 0.0f, 1.0f ),       // Green
    vec4( 0.0f, 1.0f, 1.0f, 1.0f ),       // Cyan
    vec4( 1.0f, 0.0f, 0.0f, 1.0f ),       // Red
    vec4( 1.0f, 0.0f, 1.0f, 1.0f ),       // Magenta
    vec4( 1.0f, 1.0f, 0.0f, 1.0f ),       // Yellow
    vec4( 1.01, 1.0f, 1.0f, 1.0f )        // White
};

const int indices[18] = {
    0, 1, 2,
    3, 6, 7,
    4, 5, -1,
    2, 6, 0,
    4, 1, 5,
    3, 7, -1
};

void main() {

    // AABB vertices
    const vec4 Pos[8] = {
        vec4( vMin[0].x, vMin[0].y, vMin[0].z, 1.0f ),    // 0
        vec4( vMin[0].x, vMin[0].y, vMax[0].z, 1.0f ),    // 1
        vec4( vMin[0].x, vMax[0].y, vMin[0].z, 1.0f ),    // 2
        vec4( vMin[0].x, vMax[0].y, vMax[0].z, 1.0f ),    // 3
        vec4( vMax[0].x, vMin[0].y, vMin[0].z, 1.0f ),    // 4
        vec4( vMax[0].x, vMin[0].y, vMax[0].z, 1.0f ),    // 5
        vec4( vMax[0].x, vMax[0].y, vMin[0].z, 1.0f ),    // 6
        vec4( vMax[0].x, vMax[0].y, vMax[0].z, 1.0f )     // 7
    };

    for (int i = 0; i < 18; ++i) {
        if (indices[i] == -1) {
            EndPrimitive();
        }
        else {
            gl_Position = DebugClustersMatrices.dbProjection * DebugClustersMatrices.dbView * Pos[indices[i]];
            gColor = vColor[0];
            EmitVertex();
        }
    }

}
