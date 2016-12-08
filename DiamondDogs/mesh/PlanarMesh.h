#pragma once
#ifndef PLANAR_MESH_H
#define PLANAR_MESH_H
#include "Mesh.h"

enum class CardinalFace {
	FRONT,
	BACK,
	BOTTOM,
	TOP,
	LEFT,
	RIGHT
};

class PlanarMesh : public Mesh {
public:
	PlanarMesh() = default;

	std::array<index_t, 9> GetSubfaces(const face_t &f, const PlanarMesh& from, PlanarMesh& to);

	PlanarMesh(const uint &lod, const CardinalFace &f);

	~PlanarMesh() = default;

	// Subdivides this mesh up to Max_LOD
	void Subdivide();
	// Subdivides in a way that results in optimal subdivision for mapping to a unit sphere
	void SubdivideForSphere();
	// Performs a single subdivision
	void SingleSubdivision();
	// Reverses the previous subdivision
	void ReverseSubdivision();
	// Map this plane to a unit sphere
	void ToUnitSphere();
	// Map this plane to a sphere with a given radius
	void ToSphere(const float& radius);
	// Get distance between two vertices if they were on the unit sphere
	float UnitSphereDist(const index_t& i0, const index_t &i1) const;

protected:
	// Max subdivision level.
	unsigned int Max_LOD;
	CardinalFace Face;
};

#endif // !PLANAR_MESH_H
