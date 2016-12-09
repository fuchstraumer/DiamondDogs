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

// Input vertices should be specified for CCW winding order, given as seen for relevant face
inline std::vector<vertex_t> subfacePoints(const vertex_t &v0, const vertex_t &v1, const vertex_t &v2, const vertex_t &v3) {
	std::vector<vertex_t> result;
	result.resize(9);
	result[0] = v0; 
	result[1] = getMiddlePoint(v0, v1);
	result[2] = v1; 
	result[3] = getMiddlePoint(v1, v2);
	result[4] = getMiddlePoint(v0, v2); 
	result[5] = getMiddlePoint(v0, v3);
	result[6] = v2; 
	result[7] = getMiddlePoint(v2, v3);
	result[8] = v3;
	return result;
}

/*
	

*/

// Gets all of the vertices that define the four rectangles subdividing one face.
/*		
		Old:
		v3--------v2
		|		   |
		|		   |
		|		   |
		|		   |
		v0--------v1
		
		New:
		v8----v7----v6
	    |	4  |  3  |
		|      |     |
		v5----v4----v3
		|	1  |  2  |
		|	   |     |
		v0----v1----v2

		Subface 1: v0, v1, v4, v5
		Subface 2: v1, v2, v3, v4
		Subface 3: v4, v3, v8, v7
		Subface 4: v5, v4, v7, v6
		Old vertices are at indices: v0, v2, v8, v6

*/
inline std::array<index_t, 9> PlanarMesh::GetSubfaces(const PlanarMesh& from, PlanarMesh& to) {
	std::array<index_t, 9> result;
	triangle_t t0, t1;
	result[0] = to.AddVert(from.GetVertex(t0.i0));
	result[2] = to.AddVert(from.GetVertex(t0.i1));
	result[6] = to.AddVert(from.GetVertex(t1.i1));
	result[8] = to.AddVert(from.GetVertex(t0.i2));
	// v1 is midpoint of result[0]-result[2]
	vertex_t v1 = to.GetMiddlePoint(result[0], result[2]);
	// v3 is midpoint of result[2]-result[8]
	vertex_t v3 = to.GetMiddlePoint(result[2], result[6]);
	// v5 is midpoint of result[0]-result[6]
	vertex_t v5 = to.GetMiddlePoint(result[0], result[8]);
	// v4 is midpoint of v3-v5
	vertex_t v4 = to.GetMiddlePoint(result[0], result[6]);
	// v7 is midpoint of result[6]-result[8]
	vertex_t v7 = to.GetMiddlePoint(result[6], result[8]);
	// Add our new vertices to the mesh
	result[1] = to.AddVert(v1);
	result[3] = to.AddVert(v3);
	result[4] = to.AddVert(v4);
	result[5] = to.AddVert(v5);
	result[7] = to.AddVert(v7);
	// Return our new points
	return result;
}

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

};

PlanarMesh::PlanarMesh(const uint &lod, const CardinalFace &f) {
	Max_LOD = lod;
	Face = f;
	// Generate the initial face defining this plane
	switch (Face) {
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

float PlanarMesh::UnitSphereDist(const index_t & i0, const index_t & i1) const {
	auto&& v0 = GetVertex(i0);
	auto&& v1 = GetVertex(i1);
	glm::vec3 p0, p1;
	p0 = PointToUnitSphere(v0.Position);
	p1 = PointToUnitSphere(v1.Position);

	float dist = glm::distance(p0, p1);
	
	return dist;
}




void PlanarMesh::SubdivideForSphere(){

}
