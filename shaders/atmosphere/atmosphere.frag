#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout(location = 0) in vec4 secondaryColor;
layout(location = 1) in vec4 primaryColor;
layout(location = 2) in vec3 vDir;

layout(location = 0) out vec4 frag_color;

layout(set = 0, binding = 1) uniform atmoUBO {
	vec4 cameraPos;
	vec4 lightDir;
	vec4 invWavelength;
	vec4 lightPos;
	vec4 cHoRiR; // cameraHeight-outerRadius-innerRadius
} atmo_ubo;

layout(constant_id = 0) const float g = ;

void main(){
	vec3 light_pos = normalize(atmo_ubo.lightPos.xyz - atmo_ubo.cameraPos.xyz);
	float f_cos = dot(light_pos, vDir) / length(vDir);
	float f_rayleigh = 1.0f + (f_cos * f_cos);
	float f_mie = (1.0f - (-0.99f * -0.99f)) / (2.0f + (-0.99f*-0.99f)) * (1.0f + (f_cos * f_cos)) / pow(1.0f + (-0.99f * -0.99f) - 2.0f * g * f_cos, 1.50f);
	frag_color = vec4(1.0f - exp(-1.0f * (f_rayleigh * primaryColor.rgb + f_mie * secondaryColor.rgb)), 1.0f);
	frag_color.a = primaryColor.b;
}