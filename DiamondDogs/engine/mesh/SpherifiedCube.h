#pragma once
#ifndef SPHERIFIED_CUBE_H
#define SPHERIFIED_CUBE_H
#include "stdafx.h"
#include "Mesh.h"
#include "PlanarMesh.h"



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
	// Get world coordinates from cubemap coordinates
	glm::vec3 ToCartesian(const CardinalFace & face, const glm::vec2 & cubemap_coords) const;
	// To stop precision issues, the position of each vertex is given 
	// relative to the center of this object, and transformed on the CPU
	// before rendering.
	glm::vec3 Center;
	// Scale of this object
	glm::vec3 Scale;
};

#endif // !SPHERIFIED_CUBE_H
