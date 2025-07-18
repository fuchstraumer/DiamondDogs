#include "Structures.glsl"
SPC const bool DoPointLights = true;
#pragma USE_RESOURCES GlobalResources
#pragma USE_RESOURCES VolumetricForwardLights

#pragma USE_INTERFACE SharedVertexInputs
#pragma BEGIN_CUSTOM_INTERFACE
flat out int vInstanceID;
#pragma END_CUSTOM_INTERFACE

void main() 
{
    [[flatten]]
    if (DoPointLights)
    {
        vInstanceID = gl_InstanceIndex;
        PointLight light = PointLights.Data[vInstanceID];
        vec4 transformed_position = vec4(light.Range * (light.Position.xyz + position), 1.0f);
        gl_Position = matrices.projection * matrices.view * transformed_position;
    }
    else
    {
        vInstanceID = gl_InstanceIndex;
        SpotLight light = SpotLights.Data[vInstanceID];
        vec4 transformed_position = vec4(light.Range * (light.Position.xyz + position), 1.0f);
        gl_Position = matrices.projection * matrices.view * transformed_position;

    }
}
