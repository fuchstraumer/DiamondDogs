#pragma once
#ifndef STAR_H
#define STAR_H
#include "Body.h"
#include "../mesh/Icosphere.h"

class Star : public Body {
public:
	// Creates a star, randomly selecting all values from within reasonable ranges
	Star(int lod_level, float _radius, unsigned int temp, const glm::mat4& projection);
	// Creates a star, using supplied values or reasonably shuffled defaults otherwise.

	~Star() = default;
	Star() = default;
	// Render this star, supplying the view matrix needed
	void Render(const glm::mat4& view);
private:
	// Temperature selects color, and specifies which texture coordinate to use for all
	// vertices in this object since the base color is uniform
	unsigned int temperature;
	// Radius is useful to know, but will mainly be set in the mesh since we care about it most there
	float radius;
	Icosphere mesh;
	ShaderProgram shader;
	static Texture1D starColor;
};

#endif // !STAR_H
