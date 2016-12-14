#include "../stdafx.h"
#include "SpherifiedCube.h"

// Get the distance between two input vertices that are part of Face "face" when they are mapped to the unit sphere
float SpherifiedCube::UnitSphereDist(const int& face, const index_t & i0, const index_t & i1) const {
	// Get our vertices
	auto&& v0 = Faces[face].GetVertex(i0);
	auto&& v1 = Faces[face].GetVertex(i1);
	// Get the positions on the unit sphere, by normalizing the vertex positions
	glm::vec3 p0, p1;
	p0 = glm::normalize(v0.Position);
	p1 = glm::normalize(v1.Position);
	// Get the distance between these two points, as a single float value (linear distance/displacement)
	float dist = glm::distance(p0, p1);
	return dist;
}

SpherifiedCube::SpherifiedCube(int subdivisions){
	Subdivision_Level = subdivisions;
	// Create the six faces of this cube, but don't create the triangles
	// or spherify things yet
	Faces[0] = PlanarMesh(subdivisions, CardinalFace::BACK);
	Faces[1] = PlanarMesh(subdivisions, CardinalFace::RIGHT);
	Faces[2] = PlanarMesh(subdivisions, CardinalFace::FRONT);
	Faces[3] = PlanarMesh(subdivisions, CardinalFace::LEFT);
	Faces[4] = PlanarMesh(subdivisions, CardinalFace::TOP);
	Faces[5] = PlanarMesh(subdivisions, CardinalFace::BOTTOM);
	for (int i = 0; i < 6; ++i) {
		// Make the correct triangles for each mesh
		// setting up the indices correctly
		makeUnitCubeTriangles(i);
	}
}

void SpherifiedCube::Spherify(){
	for (auto&& face : Faces) {
		// Mapping a vertex to the unit sphere is as easy as 
		// 1. Normalizing the vertices position
		// 2. Getting the new normal by finding the direction vector
		//    from the origin to the new position (and normalizing that in turn)
		for (auto iter = face.Vertices.begin(); iter != face.Vertices.end(); ++iter) {
			(*iter).Position = glm::normalize((*iter).Position);
			(*iter).Normal = (*iter).Position - glm::vec3(0.0f);
		}
	}
}

// This method uses the vertices we made earlier (placed along an angular range)
// and uses them to generate triangles. If we just generated triangles first, we 
// would see that near the corners of each plane our triangles/faces get really 
// warped. By dividing along the shortest diagonal (when mapped to the unit sphere)
// we can greatly decrease this distortion.
void SpherifiedCube::makeUnitCubeTriangles(int face){
	index_t v1, v2, v3, v4;
	for (int i = 0; i < Subdivision_Level; ++i) {
		for (int j = 0; j < Subdivision_Level; ++j) {

			// Get our vertex indices: we have a known stride amount
			// and know how large our vertex container is
			v1 = i + j * (Subdivision_Level + 1);
			v2 = v1 + 1;
			v3 = v1 + Subdivision_Level + 1;
			v4 = v3 + 1;

			// Get diagonal distances, and then divide along the shortest diagonal
			// This distance is the distance between the vertices when they are
			// mapped to the unit sphere - otherwise, this distance doesn't really
			// do anything to help the distortion.
			float v1v4Diag = UnitSphereDist(face, v1, v4);
			float v2v3Diag = UnitSphereDist(face, v2, v3);

			if (v2v3Diag < v1v4Diag) {
				// If v2v3 is the shortest diagonal, create these two triangles
				Faces[face].AddTriangle(v1, v2, v3);
				Faces[face].AddTriangle(v3, v2, v4);
			}
			else {
				// If v1v4 is the shortest instead, create these two triangles
				Faces[face].AddTriangle(v1, v2, v4);
				Faces[face].AddTriangle(v1, v4, v3);
			}
		}
	}
}

// Convert from cubemap coords to cartesian coords
// Face is one of the six faces of the spherified cube, and "cubemap_coords" 
// are the x-y coords of interest on that face.
// TODO: How to scale these to world coords correctly? currently relative to center of body iirc
glm::vec3 SpherifiedCube::ToCartesian(const CardinalFace& face, const glm::vec2 & cubemap_coords) const {
	glm::vec3 res;
	switch (face)
	{
		// TODO: got to be a better way to do this. finish later.
	case CardinalFace::BACK:
		res.x = (cubemap_coords.x - static_cast<float>(Subdivision_Level) / 2.0f) / static_cast<float>(Subdivision_Level);
		res.y = -(cubemap_coords.y - static_cast<float>(Subdivision_Level) / 2.0f) / static_cast<float>(Subdivision_Level);
		res.z = 0.50f;
	default:
		break;
	}
	return res;
}
