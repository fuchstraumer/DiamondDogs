#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable
layout(location = 0) in vec3 position;

layout(push_constant) uniform ubo_data {
	mat4 view;
	mat4 projection;
	vec4 center;
	vec4 size;
	vec4 cameraUp;
	vec4 cameraRight;
} ubo; 

out gl_PerVertex {
    vec4 gl_Position;
};

layout(location = 0) out vec4 vPos;

void main(){
    // Center of billboard in world-space
    vec3 worldSpaceCenter = ubo.center.xyz;
    // Vertex position in worldspace
    vec3 vPosWorldSpace = worldSpaceCenter + ubo.cameraRight.xyz * position.x * ubo.size.x + ubo.cameraUp.xyz * position.y * ubo.size.y;
    // Output position
    gl_Position = ubo.projection * ubo.view * vec4(vPosWorldSpace, 1.0f);
    // Output UV given to fragment shader
    vPos = vec4(position,1.0f);
}