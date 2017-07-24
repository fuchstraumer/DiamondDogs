#pragma once
#ifndef BODY_H
#define BODY_H
#include "stdafx.h"

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

};
#endif // !BODY_H
