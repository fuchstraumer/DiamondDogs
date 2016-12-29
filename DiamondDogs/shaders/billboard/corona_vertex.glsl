#version 430

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform mat4 viewTransform;

layout(location = 0) in vec3 position;
layout(location = 1) in vec2 uv;

out vec3 vPos;
out vec2 vUV;

void main(){
    vUV = uv;
    vPos = position;
    vec4 Position = projection * view * model * vec4(position, 1.0f);
    gl_Position = viewTransform * Position;
}