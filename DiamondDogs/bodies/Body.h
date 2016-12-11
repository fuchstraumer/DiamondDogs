#pragma once
#ifndef BODY_H
#define BODY_H
#include "../stdafx.h"
#include "../mesh/SpherifiedCube.h"


class Body {
public:

	Body() = default;
	~Body() = default;

	// The position of this body in the world
	glm::vec3 WorldPosition;
	// Radius of this body's surface
	float Radius;
	// Mass of this body
	double Mass;


	// Convert from cubemap coords to cartesian coords
	// Cubemap coords are 3 components as well: (face, x, y)
	// Face is the face of the cubemap, x,y is the position on
	// that face
	glm::vec3 ToCartesian(const glm::vec3& cubemap_coords);
};
#endif // !BODY_H
