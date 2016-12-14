#version 440

uniform vec3 lightPos;
uniform vec3 lightColor;

in vec3 fragPos;
in vec3 gTriDist;
in vec3 gNormal;
in vec3 gColor;
float fade(){
    vec3 d = fwidth(gTriDist);
    vec3 a3 = smoothstep(vec3(0.0f),d*1.50f,gTriDist);
    return min(min(a3.x, a3.y),a3.z);
}

out vec4 fColor;

void main(){
    vec3 norm = normalize(gNormal);
    vec4 Color = vec4(gColor, 1.0f);

	float ambientStr = 0.40f;
	vec3 ambient = ambientStr * Color.xyz;

	// Diffuse
	float diffuseStr = 0.75f;
	vec3 lightDir = normalize(lightPos - fragPos);
	float diff = max(dot(gNormal, lightDir), 0.0f);
	vec3 diffuse = diffuseStr * diff * lightColor;
    fColor = vec4(mix(vec3(0.0f),vec3(0.60f),fade()),1.0f);
	Color += fColor;
	vec4 lightRes = vec4(diffuse + ambient, 1.0f);
	fColor = Color * lightRes;
}