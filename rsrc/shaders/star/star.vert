#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout(binding = 0) uniform UBO {
	mat4 projection;
	mat4 view;
	mat4 model;
	vec3 cameraPos;
} ubo;

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;

out gl_PerVertex {
    vec4 gl_Position;
};

layout(location = 0) out vec3 fPos;
layout(location = 1) out vec3 vNorm;

void main(){
    vec4 Position = vec4(position,1.0f);
	vNorm = normal;
    fPos = (ubo.model * Position).xyz;
    gl_Position = ubo.projection * ubo.view * ubo.model * Position;
}