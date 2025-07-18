#pragma BEGIN_CUSTOM_INTERFACE
layout (location = 0) in flat uint vInstanceID;
layout (location = 0) out vec4 backbuffer;
#pragma END_CUSTOM_INTERFACE

#include "Structures.glsl"

#pragma USE_RESOURCES GlobalResources
#pragma USE_RESOURCES VolumetricForwardLights

// Update before baking pipeline. Used to set rendering mode.
SPC const bool DoPointLights = true;

void main()
{
    [[flatten]]
    if (DoPointLights)
    {
        PointLight light = PointLights.Data[vInstanceID];
        backbuffer = vec4(light.Color, 0.30f);
    }
    else
    {
        SpotLight light = SpotLights.Data[vInstanceID];
        backbuffer = vec4(light.Color,  0.3f);
    }

}