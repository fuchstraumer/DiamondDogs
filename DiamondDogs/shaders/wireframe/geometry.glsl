#version 430

uniform mat4 normTransform;

layout(triangles) in;
layout(triangle_strip, max_vertices = 3) out;
in vec4 vPosition[3];
in vec3 fPos[3];
in vec3 vNormal[3];


out vec3 fNorm;
out vec3 fragPos;
out vec3 gTriDist;
out vec3 gNormal;



void main(){
    mat3 NormalMatrix = mat3(normTransform);
    vec3 A = vPosition[2].xyz - vPosition[0].xyz;
    vec3 B = vPosition[1].xyz - vPosition[0].xyz;
    fNorm = NormalMatrix * normalize(cross(A,B));

    gTriDist = vec3(1.0f, 0.0f, 0.0f);
    fragPos = fPos[0];
    gNormal = vNormal[0];

    gl_Position = vPosition[0];
    EmitVertex();

    gTriDist = vec3(0.0f, 0.0f, 1.0f);
    fragPos = fPos[1];
    gNormal = vNormal[1];

    gl_Position = vPosition[1];
    EmitVertex();

    gTriDist = vec3(0.0f, 1.0f, 0.0f);
    fragPos = fPos[2];
    gNormal = vNormal[2];

    gl_Position = vPosition[2];
    EmitVertex();
}