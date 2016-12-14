#version 430 core

in vec3 position;

uniform mat4 view;
uniform mat4 projection;

// Only need the view/proj matrices since 
// the skybox should transform around the viewer
// constantly.

out vec3 texPos;

void main(){
    vec4 pos = projection * view * vec4(position, 1.0f);
    // We use xyww to trick the depth test into always 
    // seeing the skybox as the absolute back-most object
    gl_Position = pos.xyww;
    texPos = position;
}