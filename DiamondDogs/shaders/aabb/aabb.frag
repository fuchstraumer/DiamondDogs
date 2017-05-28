#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout(push_constant) uniform UBO {
	layout(offset = 192) vec4 inColor;
}ubo;

layout(location = 0) out vec4 fragColor;

void main(){
	fragColor = ubo.inColor;
}