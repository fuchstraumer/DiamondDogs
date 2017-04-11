#version 450 core


uniform mat4 view;
uniform mat4 projection;
uniform mat4 model;
uniform mat4 normTransform;

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;

out gl_PerVertex {
    vec4 gl_Position;
};

out vec3 fPos;

void main(){
    vec4 Position = vec4(position,1.0f);
    gl_Position = projection * view * model * Position;
    fPos = (model * Position).xyz;
}