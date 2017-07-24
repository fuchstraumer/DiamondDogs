#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 uv;

layout(location = 0) out vec3 vPos;
layout(location = 1) out vec3 vNorm;
layout(location = 2) out vec2 vUV;

layout(set = 0, binding = 0) uniform sampler2D heightmap;

layout(push_constant) uniform UBO {
	mat4 model;
	mat4 view;
	mat4 projection;
} ubo;

out gl_PerVertex {
    vec4 gl_Position;
};

void main(){
	vPos = vec3(position * mat3(ubo.model));
	vNorm = mat3(transpose(inverse(ubo.model))) * normal;
	vUV = uv;
	gl_Position = ubo.projection * ubo.view * ubo.model * vec4(position, 1.0f);
}