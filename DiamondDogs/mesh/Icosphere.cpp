#include "../stdafx.h"
#include "Icosphere.h"

// Subdivides a triangle "tri" in "to", getting vertices out of "from", adding new parts back into "to" and leaving "from"
// unmodified
inline void SubdivideTriangle(const triangle_t& tri, const Icosphere& from, Icosphere& to) {
	vertex_t v0, v1, v2;
	/*
			  v2
			  *
			/	 \
		   /	  \
		  /	       \
		 /          \
		/			 \	
	v0	*____________*	v1	

		New:
			  v2
			  *
			/	 \
		   /	  \
		v6*	      *v4
		 /          \
		/	  v3     \	
	v0	*_____*______*	v1	
	*/
	// Get initial vertices from input triangle
	v0 = from.GetVertex(tri.i0);
	v1 = from.GetVertex(tri.i1);
	v2 = from.GetVertex(tri.i2);
	// New vertices
	vertex_t v3, v4, v5;
	// Get the new vertices by getting a number of midpoints
	v3 = from.GetMiddlePoint(v0, v1);
	v4 = from.GetMiddlePoint(v1, v2);
	v5 = from.GetMiddlePoint(v0, v2);
	// Indices to new vertices
	index_t i0, i1, i2, i3, i4, i5;
	// Set the indices by adding all of our vertices, "old" and new to the destination
	i0 = to.AddVert(v0);
	i1 = to.AddVert(v1);
	i2 = to.AddVert(v2);
	i3 = to.AddVert(v3);
	i4 = to.AddVert(v4);
	i5 = to.AddVert(v5);
	// Add triangles using the previously added indices.
	to.AddTriangle(i0, i3, i5);
	to.AddTriangle(i1, i4, i3);
	to.AddTriangle(i2, i5, i4);
	to.AddTriangle(i3, i4, i5);
}

Icosphere::Icosphere(unsigned int lod_level, float radius, glm::vec3 position, glm::vec3 rotation) {
	// Set properties affecting this mesh
	Position = position;
	// We are generating a sphere: scale uniformly with magnitude given by radius.
	Scale = glm::vec3(radius);
	Angle = rotation;
	LOD_Level = lod_level;
	// Routine for generating the actual mesh
	// Constants affecting position of initial verts
	const float x = 0.525731112119133606f;
	const float z = 0.850650808352039932f;
	const float n = 0.0f;

	// Creation of individual vertices.
	static const std::vector<vertex_t> initialVertices = {
		vertex_t(glm::vec3(-x,n,z)),vertex_t(glm::vec3(x,n,z)),
		vertex_t(glm::vec3(-x,n,-z)),vertex_t(glm::vec3(x,n,-z)),
		vertex_t(glm::vec3(n,z,x)),vertex_t(glm::vec3(n,z,-x)),
		vertex_t(glm::vec3(n,-z,x)),vertex_t(glm::vec3(n,-z,-x)),
		vertex_t(glm::vec3(z,x,n)),vertex_t(glm::vec3(-z,x,n)),
		vertex_t(glm::vec3(z,-x,n)),vertex_t(glm::vec3(-z,-x,n)),
	};
	// Add verts to mesh
	for (auto vert : initialVertices) {
		AddVert(vert);
	}
	// Specification of triangles.
	static const std::vector<triangle_t> initialTris = {
		triangle_t(0,4,1),triangle_t(0,9,4),triangle_t(9,5,4),triangle_t(4,5,8),triangle_t(4,8,1),
		triangle_t(8,10,1),triangle_t(8,3,10),triangle_t(5,3,8),triangle_t(5,2,3),triangle_t(2,7,3),
		triangle_t(7,10,3),triangle_t(7,6,10),triangle_t(7,11,6),triangle_t(11,0,6),triangle_t(0,1,6),
		triangle_t(6,1,10),triangle_t(9,0,11),triangle_t(9,11,2),triangle_t(9,2,5),triangle_t(7,2,11),
	};
	// Add triangles to mesh, which have indices to the vertices we added earlier
	for (auto tri : initialTris) {
		AddTriangle(tri);
	}
	// Subdivide now, using LOD_Level to control the outer loop and using triangles in a temp mesh to control the inner
	for (unsigned int i = 0; i < LOD_Level; ++i) {
		Icosphere temp;
		for (auto&& tri : this->Triangles) {
			SubdivideTriangle(tri, *this, temp);
		}
		*this = std::move(temp);
		temp.Clear();
	}
}
