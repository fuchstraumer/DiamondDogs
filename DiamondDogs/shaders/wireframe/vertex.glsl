#version 440

uniform mat4 view;
uniform mat4 projection;
uniform mat4 model;
uniform mat4 normTransform;

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec3 color;

out vec3 fPos;
out vec4 vPosition;
out vec3 vNormal;
out vec3 vColor;

void main(){
    vec4 Position = vec4(position,1.0f);
    vNormal = mat3(normTransform) * normal;
    vPosition = projection * view * model * Position;
	vColor = color;
    fPos = (model * Position).xyz;
}