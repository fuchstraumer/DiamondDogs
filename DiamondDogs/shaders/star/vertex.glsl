#version 440

uniform mat4 view;
uniform mat4 projection;
uniform mat4 model;
uniform mat4 normTransform;

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;


out vec3 fPos;
out vec3 vNormal;

void main(){
    vec4 Position = vec4(position,1.0f);
    vNormal = mat3(normTransform) * normal;
    gl_Position = projection * view * model * Position;
    fPos = (model * Position).xyz;
}