#pragma once
#ifndef CUBE_SPHERE_H
#define CUBE_SPHERE_H

#include "../stdafx.h"
#include "../mesh/PlanarMesh.h"

/*
	CUBE_SPHERE_H

	A cube-sphere is a spherified cube, where six planar faces making up a cube are warped
	to make a sphere (this is done by mapping them to the unit circle, then multiplying
	by a constant radius). This class isn't just a mesh interface: it can also be used
	to change properties of the whole sphere at a time, generate terrain, and change other
	parameters that control this meta-object

*/

class CubeSphere {
public:
	CubeSphere(uint lod, float radius);

	~CubeSphere() = default;

	float SphereDistance(const index_t& i0, const index_t& v1) const;
};
#endif // !CUBE_SPHERE_H
