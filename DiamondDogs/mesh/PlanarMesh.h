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

	PlanarMesh(const uint &lod, const CardinalFace &f, const char* colorTex);

	~PlanarMesh() = default;

	// Map this plane to a unit sphere
	void ToUnitSphere();
	// Map this plane to a sphere with a given radius
	void ToSphere(const float& radius);
	// Get distance between two vertices if they were on the unit sphere
	float UnitSphereDist(const index_t& i0, const index_t &i1) const;

protected:
	// Max subdivision level.
	int Max_LOD;
	CardinalFace Face;
	
};

#endif // !PLANAR_MESH_H
