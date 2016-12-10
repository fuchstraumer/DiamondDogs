#pragma once
#ifndef SPHERIFIED_CUBE_H
#define SPHERIFIED_CUBE_H
#include "../stdafx.h"
#include "Mesh.h"
#include "PlanarMesh.h"
#include <array>


class SpherifiedCube {
public:
	float UnitSphereDist(const int& face, const index_t & i0, const index_t & i1) const;
	SpherifiedCube(int subdivisions);
	void IncreaseLOD(int subdivisions);
	SpherifiedCube() = default;
	~SpherifiedCube() = default;
	void Spherify();
	int Subdivision_Level;
	std::array<PlanarMesh, 6> Faces;
protected:
	void makeUnitCubeTriangles(int face);
};

#endif // !SPHERIFIED_CUBE_H
