#pragma once
#ifndef MODS_H
#define MODS_H
#include "../../stdafx.h"
// Bunch of functions for modulating and modifying output of noise functions
// Takes an input value and does something to it, and returns that value. Generally
// fairly simple. Some of these are setup as classes, so that they do the something
// with the same set of parameters repeatedly. 

/*
	
	Masks:

	Masks are used to modulate a value based on position.

	For now, there are only spherical masks available. Given
	an input position and a noise value at that position,
	the mask modulates the value based on the position relative
	to a point given on making this class and returns it

	This first mask creates a circle at a position given, with a radius given

*/

class CircleMask {
public:
	CircleMask(const glm::vec3& center, const float& radius) {
		origin = center;
		dist = glm::length(origin);
	}

	float GetValue(const glm::vec3& position, const float& val) const {

	}
protected:
	glm::vec3 origin;
	float dist;
};

#include <queue>
/*
	Curve:

	Modulates the input values based on the series
	of control points given that are used to make
	a curve to map the input to


*/

struct controlPoint {
	float in;
	float out;

	bool operator<(const controlPoint& c0) {
		if (this->in > c0.in) {
			return false;
		}
		else if(this->in < c0.in) {
			return true;
		}
	}
};



class Curve {
public:
	std::priority_queue<controlPoint> controlPoints;
};

#endif // !MODS_H
