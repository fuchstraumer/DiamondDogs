#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout(location = 0) in vec4 secondaryColor;
layout(location = 1) in vec4 primaryColor;
layout(location = 2) in vec3 vDir;

layout(location = 0) out vec4 frag_color;

layout(set = 0, binding = 1) uniform atmoUBO {
	vec3 cameraPos;
	vec3 lightDir;
	float cameraHeight;
	float outerRadius;
	float innerRadius;
	vec3 invWavelength;
	vec3 lightPos;
} atmo_ubo;

layout(constant_id = 0) const float g = -0.99f;

void main(){
	float f_cos = dot(atmo_ubo.lightPos, vDir) / length(vDir);
	float f_rayleigh = 1.0f + (f_cos * f_cos);
	float f_mie = (1.0f - (g * g)) / (2.0f + (g*g)) * (1.0f + (f_cos * f_cos)) / pow(1.0f + (g * g) - 2.0f * g * f_cos, 1.50f);
	frag_color = vec4(1.0f - exp(-1.0f * (f_rayleigh * primaryColor.rgb + f_mie * secondaryColor.rgb)), 1.0f);
	frag_color.a = primaryColor.b;
}