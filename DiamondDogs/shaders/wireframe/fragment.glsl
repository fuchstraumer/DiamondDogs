#version 430

uniform vec3 lightPos;
uniform vec3 lightColor;
uniform vec3 viewPos;

in vec3 fragPos;
in vec3 gTriDist;
in vec3 gNormal;

float fade(){
    vec3 d = fwidth(gTriDist);
    vec3 a3 = smoothstep(vec3(0.0f),d*1.50f,gTriDist);
    return min(min(a3.x, a3.y),a3.z);
}

out vec4 fColor;

void main(){
    vec4 norm = vec4(gNormal,1.0f);
	vec4 Color = vec4(0.9f,0.9f,0.9f,1.0f);

	float ambientStr = 0.60f;
	vec3 ambient = ambientStr * Color.xyz;

	// Diffuse
	float diffuseStr = 0.85f;
	vec3 lightDir = normalize(lightPos - fragPos);
	float diff = max(dot(norm.xyz, lightDir), 0.0f);
	vec3 diffuse = diffuseStr * diff * lightColor;

	// Specular
	float specularStrength = 0.15f;
	vec3 viewDir = normalize(viewPos - fragPos);
	vec3 reflectDir = reflect(-lightDir, norm.xyz);
	float spec = pow(max(dot(viewDir,reflectDir),0.0),32);
	vec3 specular = specularStrength * spec * lightColor;

    fColor = vec4(mix(vec3(0.0f),vec3(0.10f),fade()),1.0f);
	Color += fColor;
	vec4 lightRes = vec4(diffuse + ambient + specular, 1.0f);
	fColor = Color * lightRes;
}