#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout(push_constant) uniform UBO {
	layout(offset = 192) vec4 inColor;
	layout(offset = 208) vec4 viewPos;
}ubo;

layout(location = 0) in vec3 vPos;
layout(location = 1) in vec3 vNorm;
layout(location = 2) in vec2 vUV;

layout(location = 0) out vec4 fragColor;

void main(){

	const vec3 lightPos = vec3(0.0f, 3000.0f, 0.0f);
	const vec3 lightColor = vec3(0.99f, 0.99f, 0.88f);

	// Ambient lighting
	float ambientStrength = 0.3f;
	vec3 ambient = ambientStrength*lightColor;

	// Diffuse
	float diffuseStrength = 0.75f;
	vec3 lightDir = normalize(lightPos - vPos);
	vec3 norm = vNorm;
	float diff = max(dot(norm,lightDir),0.0);
	vec3 diffuse = diffuseStrength * diff * lightColor;

	// Specular
	float specularStrength = 0.15f;
	vec3 viewDir = normalize(ubo.viewPos.xyz - vPos);
	vec3 reflectDir = reflect(-lightDir, norm);
	float spec = pow(max(dot(viewDir,reflectDir),0.0),32);
	vec3 specular = specularStrength * spec * lightColor;


	// Resultant lighting
	vec4 lightResult = vec4(ambient+diffuse+specular,1.0f);

	fragColor = ubo.inColor * lightResult;
}