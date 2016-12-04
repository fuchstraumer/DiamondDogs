#include "../stdafx.h"
#include "PlanarMesh.h"

// Prebuilt array of vertices useful for making cubes/planes
static const std::array<glm::vec3, 8> vertices{
	{ glm::vec3(-1.0f, -1.0f, +1.0f), // Point 0, left lower front UV{0,0}
	glm::vec3(+1.0f, -1.0f, +1.0f), // Point 1, right lower front UV{1,0}
	glm::vec3(+1.0f, +1.0f, +1.0f), // Point 2, right upper front UV{1,1}
	glm::vec3(-1.0f, +1.0f, +1.0f), // Point 3, left upper front UV{0,1}
	glm::vec3(+1.0f, -1.0f, -1.0f), // Point 4, right lower rear
	glm::vec3(-1.0f, -1.0f, -1.0f), // Point 5, left lower rear
	glm::vec3(-1.0f, +1.0f, -1.0f), // Point 6, left upper rear
	glm::vec3(+1.0f, +1.0f, -1.0f) } // Point 7, right upper rear
};

// Pre-built array of normals. 
static const std::array<glm::vec3, 6> normals{
	{ glm::ivec3(0, 0, 1),   // (front)
	glm::ivec3(0, 0,-1),   // (back)
	glm::ivec3(0, 1, 0),   // (top)
	glm::ivec3(0,-1, 0),   // (bottom)
	glm::ivec3(1, 0, 0),   // (right)
	glm::ivec3(-1, 0, 0), }  // (left)
};

// Builds a face given input vertices and norms, adds the verts to the mesh, and returns a face_t
inline auto buildface(int norm, glm::vec3 p0, glm::vec3 p1, glm::vec3 p2, glm::vec3 p3, Mesh& mesh) {
	// We'll need four indices and four vertices for the two tris defining a face.
	index_t i0, i1, i2, i3;
	vertex_t v0, v1, v2, v3;
	// Set the vertex positions based on provided input positions (from verts array above)
	v0.Position.xyz = p0;
	v1.Position.xyz = p1;
	v2.Position.xyz = p2;
	v3.Position.xyz = p3;
	// Set vertex normals using values from normal array
	v0.Normal = normals[norm];
	v1.Normal = normals[norm];
	v2.Normal = normals[norm];
	v3.Normal = normals[norm];
	// Add the verts to the Mesh's vertex container. Returns index to added vert.
	i0 = mesh.AddVert(v0);
	i1 = mesh.AddVert(v1);
	i2 = mesh.AddVert(v2);
	i3 = mesh.AddVert(v3);
	// Create the face with the given indices, but don't add it yet
	face_t result = mesh.CreateFace(i0, i1, i2, i3);
	mesh.AddFace(result);
};

PlanarMesh::PlanarMesh(const uint &lod, const CardinalFace &f) {
	Max_LOD = lod;
	face = f;
	// Generate the initial face defining this plane
	switch (face) {
	case CardinalFace::FRONT:
		buildface(0, vertices[0], vertices[1], vertices[2], vertices[3], *this);
		break;
	case CardinalFace::BACK:
		buildface(1, vertices[4], vertices[5], vertices[6], vertices[7], *this);
		break;
	case CardinalFace::TOP:
		buildface(2, vertices[3], vertices[2], vertices[7], vertices[6], *this);
		break;
	case CardinalFace::BOTTOM:
		buildface(3, vertices[5], vertices[4], vertices[1], vertices[0], *this);
		break;
	case CardinalFace::RIGHT:
		buildface(4, vertices[1], vertices[4], vertices[7], vertices[2], *this);
		break;
	case CardinalFace::LEFT:
		buildface(5, vertices[5], vertices[0], vertices[3], vertices[6], *this);
		break;
	}
}