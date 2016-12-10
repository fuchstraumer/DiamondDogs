#include "../stdafx.h"
#include "SpherifiedCube.h"

float SpherifiedCube::UnitSphereDist(const int& face, const index_t & i0, const index_t & i1) const {
	auto&& v0 = Faces[face].GetVertex(i0);
	auto&& v1 = Faces[face].GetVertex(i1);
	glm::vec3 p0, p1;
	p0 = glm::normalize(v0.Position);
	p1 = glm::normalize(v1.Position);

	float dist = glm::distance(p0, p1);

	return dist;
}

SpherifiedCube::SpherifiedCube(int subdivisions){
	Subdivision_Level = subdivisions;
	Faces[0] = PlanarMesh(subdivisions, CardinalFace::BACK);
	Faces[1] = PlanarMesh(subdivisions, CardinalFace::RIGHT);
	Faces[2] = PlanarMesh(subdivisions, CardinalFace::FRONT);
	Faces[3] = PlanarMesh(subdivisions, CardinalFace::LEFT);
	Faces[4] = PlanarMesh(subdivisions, CardinalFace::TOP);
	Faces[5] = PlanarMesh(subdivisions, CardinalFace::BOTTOM);

	for (int i = 0; i < 6; ++i) {
		makeUnitCubeTriangles(i);
	}
}

void SpherifiedCube::Spherify(){

}

void SpherifiedCube::makeUnitCubeTriangles(int face){
	index_t v1, v2, v3, v4;
	//tIndex = face * (Subdivision_Level * Subdivision_Level) * 2;
	for (int i = 0; i < Subdivision_Level; ++i) {
		for (int j = 0; j < Subdivision_Level; ++j) {
			// Get our vertex indices: we have a known stride amount
			// and know how large our vertex container is
			v1 = i + j * (Subdivision_Level + 1);
			v2 = v1 + 1;
			v3 = v1 + Subdivision_Level + 1;
			v4 = v3 + 1;
			// Get diagonal distances, and then divide along the shortest diagonal
			float v1v4Diag = UnitSphereDist(face, v1, v4);
			float v2v3Diag = UnitSphereDist(face, v2, v3);

			if (v2v3Diag < v1v4Diag) {
				Faces[face].AddTriangle(v1, v2, v3);
				Faces[face].AddTriangle(v3, v2, v4);
			}
			else {
				Faces[face].AddTriangle(v1, v2, v4);
				Faces[face].AddTriangle(v1, v4, v3);
			}
		}
	}
}
