#pragma once
#ifndef TERRESTRIAL_H
#define TERRESTRIAL_H
#include "Body.h"
#include "../mesh/IcoSphere.h"
#include "../util/Shader.h"

class Terrestrial : public Body {
	Terrestrial(float radius, double mass, int LOD, float atmo_radius = 1.0f, float atmo_density = 1.0f) : Body(LOD) {
		Radius = radius;
		Mass = mass;
		if (atmo_radius == 1.0f) {
			AtmoRadius = 1.20f * Radius;
		}
		else {
			AtmoRadius = atmo_radius;
		}
		AtmoDensity = atmo_density;
		Atmosphere = IcoSphere(static_cast<unsigned int>(LOD), AtmoRadius);
	}

	// Controls radius of the atmosphere
	float AtmoRadius;
	// Controls density of the atmosphere.
	float AtmoDensity;
	// Controls the refraction spectrum of this atmosphere, or its color
	glm::vec4 AtmoSpectrum;

private:

	// Mesh used to generate the atmosphere
	IcoSphere Atmosphere;
	// Shader for the atmosphere alone
	ShaderProgram AtmoShader;
};
#endif // !TERRESTRIAL_H
