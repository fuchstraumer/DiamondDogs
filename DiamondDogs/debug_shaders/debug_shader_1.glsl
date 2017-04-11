#version 450 core
uniform mat4 projection;
uniform mat4 view;
uniform vec3 center;
// Size of the billboard, in world units (i.e radius of star + size of this object)
uniform vec2 size;
// Camera "up" vector. 
uniform vec3 cameraUp;
// Camera "right" vector
uniform vec3 cameraRight;

layout(location = 0) in vec3 position;

out gl_PerVertex {
    vec4 gl_Position;
};

out vec4 vPos;

void main(){
    // Center of billboard in world-space
    vec3 worldSpaceCenter = center;
    // Vertex position in worldspace
    vec3 vPosWorldSpace = worldSpaceCenter + cameraRight * position.x * size.x + 
    cameraUp * position.y * size.y;
    // Output position
    gl_Position = projection * view * vec4(vPosWorldSpace, 1.0f);
    // Output UV given to fragment shader
    vPos = vec4(position,1.0f);
}