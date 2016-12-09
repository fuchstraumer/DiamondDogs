#pragma once
#ifndef SPHERIFIED_CUBE_H
#define SPHERIFIED_CUBE_H
#include "../stdafx.h"
#include "Mesh.h"



class SpherifiedCube : public Mesh {
public:
	float UnitSphereDist(const index_t & i0, const index_t & i1) const;
	SpherifiedCube(int subdivisions);
	void Spherify();
	int Subdivision_Level;
protected:
	void makeUnitCubeTriangles(int face);
};

#endif // !SPHERIFIED_CUBE_H
