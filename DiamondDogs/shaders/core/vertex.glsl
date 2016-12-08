#version 400

uniform mat4 view;
uniform mat4 projection;
uniform mat4 model;
uniform mat4 normTransform;

in vec3 position;
in vec3 normal;

out vec3 fPos;
out vec3 vNormal;

void main(){
    vec4 Position = vec4(position,1.0f);
    vNormal = mat3(normTransform) * normal;
    vec4 vPosition = projection * view * model * Position;
    fPos = (model * Position).xyz;
    gl_Position = vPosition;
}