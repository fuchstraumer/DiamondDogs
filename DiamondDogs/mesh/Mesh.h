#pragma once
#ifndef MESH_H
#define MESH_H
#include "../stdafx.h"
#include <unordered_map>
#include <cstdint>
#include "MeshComponents.h"
// Get middle point between two vertices
class Mesh {
public:
	
	void Clear();
	// Methods
	GLsizei GetNumIndices(void) const;
	GLsizei GetNumVerts(void) const;
	// Returns references to the relevant elements
	const vertex_t& GetVertex(index_t index);
	const index_t& GetIndex(index_t index) const;
	const face_t& GetFace(index_t f_index) const;
	const tri_t& GetTri(index_t t_index) const;
	// Functions for adding to the mesh
	// Add the vertex to the mesh and return it's index
	index_t AddVert(const vertex_t& vert);
	// Add triangle to the mesh, using three indices specified, return index
	index_t AddTriangle(const index_t &i0, const index_t &i1, const index_t &i2);
	// Add a face to the mesh, return its index
	index_t AddFace(const face_t& face);
	// Functions for creating mesh data structures
	// Create a face and add its vertices to the mesh
	face_t CreateFace(const index_t &i0, const index_t &i1, const index_t &i2, const index_t &i3);

	edge_key LongestEdge(tri_t const & tri);

	index_t SplitEdge(edge_key const & edge);

	void SubdivideTriangles();

	vertex_t VertToSphere(vertex_t in, float radius);
	
	// Members
	std::vector<index_t> Indices;
	std::vector<vertex_t> Vertices;
	std::vector<tri_t> Triangles;
	std::vector<face_t> Faces;
	// Unordered map that keeps all edges in mesh,
	// used to check for need to subdivide later.
	EdgeLookup Edges;

};

#endif // !MESH_H