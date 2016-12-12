#pragma once
#ifndef GLOBE_MESH_H
#define GLOBE_MESH_H
#include "Mesh.h"
/*

	GLOBE_MESH_H

	Simple globular mesh, created using simple angular subdivision.
	As such, this mesh is subject to large amounts of distortion. This
	mesh is best used as the bounding volume for more complex spherical
	meshes, as the atmosphere mesh for planets, or in other uses where
	its inaccuracies and issues are irrelevant

*/

class GlobeMesh : public Mesh {
public:
	GlobeMesh(unsigned int lod);
	GlobeMesh() = default;
	~GlobeMesh() = default;
};
#endif // !GLOBE_MESH_H
