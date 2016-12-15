#pragma once
#ifndef MESH_H
#define MESH_H
#include "../stdafx.h"
#include "../util/Shader.h"
#include "../util/lodeTexture.h"
#include <unordered_map>
#include <cstdint>
#include "MeshComponents.h"
// Get middle point between two vertices
class Mesh {
public:
	Mesh() = default;
	~Mesh() = default;
	
	// Clears this mesh and attempts to free memory using "shrink_to_fit()"
	void Clear();

	// Returns number of indices in this mesh
	GLsizei GetNumIndices(void) const;
	// Returns number of vertices in this mesh
	GLsizei GetNumVerts(void) const;

	// Returns references to the relevant elements
	const vertex_t& GetVertex(index_t index) const;
	const index_t& GetIndex(index_t index) const;
	const triangle_t& GetTri(index_t t_index) const;

	// Functions for adding items to the mesh. Most return indices to the relevant container

	// Add the vertex to the mesh and return it's index
	index_t AddVert(const vertex_t& vert);

	// Add triangle to the mesh, using three indices specified, return index
	index_t AddTriangle(const index_t &i0, const index_t &i1, const index_t &i2);

	// Return the longest edge in a given triangle tri
	edge_key LongestEdge(triangle_t const & tri) const;

	// Splits the input edge, first checking to make sure we actually need to add a new vert
	// Returns index to the newly added vertex, or an index to whatever vertex already works
	index_t SplitEdge(edge_key const & edge);

	// Subdivides all the triangles in this mesh
	void SubdivideTriangles();

	// Maps an input vertex to a sphere
	vertex_t VertToSphere(vertex_t in, float radius) const;

	// Maps an input vertex to a unit sphere
	vertex_t VertToUnitSphere(const vertex_t& in) const;

	glm::vec3 PointToUnitSphere(const glm::vec3 & in) const;

	// Get the vertex in between the two vertices given by i0 and i1
	vertex_t GetMiddlePoint(const index_t & i0, const index_t & i1);
	
	// Members
	std::vector<index_t> Indices;
	std::vector<vertex_t> Vertices;
	std::vector<triangle_t> Triangles;
	// Unordered map that keeps all edges in mesh,
	// used to check for need to subdivide later.
	EdgeLookup Edges;

	void BuildRenderData();
	// Renders this object using the given shader program
	void Render(ShaderProgram& shader) const;

	// Used to access GL buffer/array objects
	GLuint VAO, VBO, EBO;
	// Matrix defining this mesh's world transformation
	glm::mat4 Model;
	// This matrix is commonly used by the fragment shader
	glm::mat4 NormTransform;
	// This vector defines the position of this mesh in the world (barycentric)
	glm::vec3 Position;
	// Scale of this object
	glm::vec3 Scale;
	// Angle of this object relative to world axis
	glm::vec3 Angle = glm::vec3(0.0f, 0.0f, 0.0f);
	// Tells us whether or not this object is ready to render
	bool meshBuilt = false;

	// Color texture
	Texture2D texture;
	// Normal texture
	Texture2D normal;
	// Specular texture
	Texture2D spec;
};

#endif // !MESH_H