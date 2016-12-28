#pragma once
#ifndef STAR_H
#define STAR_H
#include "Body.h"
#include "../mesh/Icosphere.h"

class Star : public Body {
public:
	// Creates a star, randomly selecting all values from within reasonable ranges

	// Creates a star, using supplied values or reasonably shuffled defaults otherwise.

	~Star() = default;
private:
	// Temperature selects color, and specifies which texture coordinate to use for all
	// vertices in this object since the base color is uniform
	unsigned int temperature;
	// Radius is useful to know, but will mainly be set in the mesh since we care about it most there
	float radius;
	Icosphere mesh;
	ShaderProgram shader;
	Texture1D starColor;
};

#endif // !STAR_H
