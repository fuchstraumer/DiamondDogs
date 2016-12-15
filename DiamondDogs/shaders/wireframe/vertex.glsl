#version 430

uniform mat4 view;
uniform mat4 projection;
uniform mat4 model;
uniform mat4 normTransform;

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 uv;

out vec3 fPos;
out vec4 vPosition;
out vec3 vNormal;
out vec2 vUV;

void main(){
    vec4 Position = vec4(position,1.0f);
    vNormal = mat3(normTransform) * normal;
    vPosition = projection * view * model * Position;
	vUV = uv;
    fPos = (model * Position).xyz;
}