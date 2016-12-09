#include "../stdafx.h"
#include "SpherifiedCube.h"

float SpherifiedCube::UnitSphereDist(const index_t & i0, const index_t & i1) const {
	auto&& v0 = GetVertex(i0);
	auto&& v1 = GetVertex(i1);
	glm::vec3 p0, p1;
	p0 = glm::normalize(v0.Position);
	p1 = glm::normalize(v1.Position);

	float dist = glm::distance(p0, p1);

	return dist;
}

SpherifiedCube::SpherifiedCube(int subdivisions){
	Subdivision_Level = subdivisions;
	// We will subdivide in an angular fashion,
	// and these are the angles that will govern it
	float xAngle, yAngle, zAngle;
	// This controls the size we step in angular terms
	const float dAngle = ((90.0f * (3.14159265f)) / 180.0f) / (static_cast<float>(Subdivision_Level));
	// Initial angle
	const float aInitial = 45.0f * (3.14159265) / 180.0f;

	// Reserve the space we need in the containers for this mesh
	int reserveAmt = (Subdivision_Level + 1) * (Subdivision_Level + 1);
	Vertices.reserve(6 * reserveAmt);
	Indices.reserve(8 * reserveAmt);
	Triangles.reserve(6 * reserveAmt);
	Subdivision_Level += 1;
	// First face (of the six), normal is positive Z
	glm::vec3 Normal = glm::vec3(0.0f, 0.0f, 1.0f);
	for (int i = 0; i < Subdivision_Level; ++i) {
		for (int j = 0; j < Subdivision_Level; ++j) {
			xAngle = aInitial - (dAngle * i);
			yAngle = -aInitial + (dAngle * j);
			vertex_t newVert;
			newVert.Position.x = static_cast<float>(tan(xAngle));
			newVert.Position.y = static_cast<float>(tan(yAngle));
			newVert.Position.z = 1.0f;
			// Temp normal, will change when we map this to a sphere
			newVert.Normal = Normal;
			// Do something with index?
			AddVert(newVert);
		}
	}

	// Second face, normal is positive X
	Normal = glm::vec3(1.0f, 0.0f, 0.0f);
	for (int i = 0; i < Subdivision_Level; ++i) {
		for (int j = 0; j < Subdivision_Level; ++j) {
			yAngle = aInitial - (dAngle * j);
			zAngle = aInitial - (dAngle * i);
			vertex_t newVert;
			newVert.Position.x = 1.0f;
			newVert.Position.y = static_cast<float>(tan(yAngle));
			newVert.Position.z = static_cast<float>(tan(zAngle));
			// Temp normal, will change when we map this to a sphere
			newVert.Normal = Normal;
			// Do something with index?
			AddVert(newVert);
		}
	}

	// Third face, normal is negative Z
	Normal = glm::vec3(0.0f, 0.0f, -1.0f);
	for (int i = 0; i < Subdivision_Level; ++i) {
		for (int j = 0; j < Subdivision_Level; ++j) {
			xAngle = -aInitial + (dAngle * i);
			yAngle = -aInitial + (dAngle * j);
			vertex_t newVert;
			newVert.Position.x = static_cast<float>(tan(xAngle));
			newVert.Position.y = static_cast<float>(tan(yAngle));
			newVert.Position.z = -1.0f;
			newVert.Normal = Normal;
			AddVert(newVert);
		}
	}

	// Fourth face, normal is negative X
	Normal = glm::vec3(-1.0f, 0.0f, 0.0f);
	for (int i = 0; i < Subdivision_Level; ++i) {
		for (int j = 0; j < Subdivision_Level; ++j) {
			yAngle = -aInitial + (dAngle * i);
			zAngle = aInitial - (dAngle * j);
			vertex_t newVert;
			newVert.Position.x = -1.0f;
			newVert.Position.y = static_cast<float>(tan(yAngle));
			newVert.Position.z = static_cast<float>(tan(zAngle));
			newVert.Normal = Normal;
			AddVert(newVert);
		}
	}

	// Fifth face, normal is positive y
	Normal = glm::vec3(0.0f, 1.0f, 0.0f);
	for (int i = 0; i < Subdivision_Level; ++i) {
		for (int j = 0; j < Subdivision_Level; ++j) {
			xAngle = aInitial - (dAngle * i);
			zAngle = aInitial - (dAngle * j);
			vertex_t newVert;
			newVert.Position.x = static_cast<float>(tan(xAngle));
			newVert.Position.y = 1.0f;
			newVert.Position.z = static_cast<float>(tan(zAngle));
			AddVert(newVert);
		}
	}

	// Sixth face, normal is Negative Y
	Normal = glm::vec3(0.0f, -1.0f, 0.0f);
	for (int i = 0; i < Subdivision_Level; ++i) {
		for (int j = 0; j < Subdivision_Level; ++j) {
			xAngle = aInitial - (dAngle * i);
			zAngle = -aInitial + (dAngle * j);
			vertex_t newVert;
			newVert.Position.x = static_cast<float>(tan(xAngle));
			newVert.Position.y = -1.0f;
			newVert.Position.z = static_cast<float>(tan(zAngle));
			AddVert(newVert);
		}
	}

	Subdivision_Level -= 1;

	for (int i = 0; i < 6; ++i) {
		makeUnitCubeTriangles(i);
	}
}

void SpherifiedCube::Spherify(){
	for (auto iter = Vertices.begin(); iter != Vertices.end(); ++iter) {
		//(*iter) = VertToUnitSphere(*iter);
		(*iter).Position = glm::normalize((*iter).Position);
	}
}

void SpherifiedCube::makeUnitCubeTriangles(int face){
	index_t vIndex;
	index_t v1, v2, v3, v4;
	vIndex = face * (Subdivision_Level + 1) * (Subdivision_Level + 1);
	//tIndex = face * (Subdivision_Level * Subdivision_Level) * 2;
	for (int i = 0; i < Subdivision_Level; ++i) {
		for (int j = 0; j < Subdivision_Level; ++j) {
			// Get our vertex indices: we have a known stride amount
			// and know how large our vertex container is
			v1 = vIndex + i + j * (Subdivision_Level + 1);
			v2 = v1 + 1;
			v3 = v1 + Subdivision_Level + 1;
			v4 = v3 + 1;
			// Get diagonal distances, and then divide along the shortest diagonal
			float v1v4Diag = UnitSphereDist(v1, v4);
			float v2v3Diag = UnitSphereDist(v2, v3);

			if (v2v3Diag < v1v4Diag) {
				AddTriangle(v1, v2, v3);
				AddTriangle(v3, v2, v4);
			}
			else {
				AddTriangle(v1, v2, v4);
				AddTriangle(v1, v4, v3);
			}
		}
	}
}
