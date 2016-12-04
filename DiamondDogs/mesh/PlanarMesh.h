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

	PlanarMesh(const uint &lod, const CardinalFace &f);

	~PlanarMesh() = default;

protected:
	// Max subdivision level.
	unsigned int Max_LOD;
	CardinalFace face;
};

#endif // !PLANAR_MESH_H
