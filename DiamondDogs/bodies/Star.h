#pragma once
#ifndef STAR_H
#define STAR_H
#include "Body.h"
#include "../mesh/GlobeMesh.h"
#include "../util/Shader.h"
/*
	
	STAR_H

	Objects of this type are stars, and will be rendered with appropriate
	properties to match their type. Spectral types can be specified, and the
	spectrum of a star can be changed. The base mesh is an icosphere, since
	this is is easier to render/build and we don't need the properties unique
	to cubemaps

*/

class Star : public Body {
public:
	Star(glm::vec3 spectrum, glm::vec3 position, float radius, int LOD) {
		Mesh = GlobeMesh(LOD);
		Radius = radius;
		Mesh.Scale = glm::vec3(Radius);
		WorldPosition = position;
		Mesh.Position = glm::vec3(WorldPosition);
		starSpectrum = spectrum;
	}

private:
	glm::vec3 starSpectrum;
	GlobeMesh Mesh;
	ShaderProgram StarProgram;
};
#endif // !STAR_H
