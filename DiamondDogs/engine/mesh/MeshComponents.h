#pragma once
#ifndef MESH_COMPONENTS_H
#define MESH_COMPONENTS_H
#include "stdafx.h"

/*
	MESH_COMPONENTS_H

	This header contains definitions and implementations for the Mesh
	components. This includes:

	vertex_t - default vertex type
	index_t - default index type
	triangle_t - templated triangle type, template parameter is the index type. Set to default to index_t.

*/


// Index type: uint16_t would be better for the GPU, but this messes up rendering and many of my meshes
// already run into the limits of 16-bit precision. Investigate this more?
using index_t = std::uint32_t;

// Vertex struct
struct vertex_t {
public:

	// Default constructor.
	vertex_t(glm::vec3 pos = glm::vec3(0.0f), glm::vec3 norm = glm::vec3(0.0f), glm::vec2 uv = glm::vec2(0.0f)) : Position(pos), Normal(norm), UV(uv) { }

	// Default destructor. Specified so this class be used as an abstract base class.
	~vertex_t() = default;

	// Position of this vertex
	glm::vec3 Position;

	// Normal of this vertex
	glm::vec3 Normal;

	// Texture coordinate to be used for this vertex
	glm::vec2 UV;
};

// Triangle struct. Holds indices to member vertices. Useful for geometry queries. 
template<typename index_type = index_t>
struct triangle_t {

	// Create a triangle using the three vertex indices giving.
	triangle_t(index_type const &i0, index_type const &i1, index_type const &i2) {
		this->i0 = i0;
		this->i1 = i1;
		this->i2 = i2;
	}

	// Default empty constructor
	triangle_t() { }

	// Vertex indices for this triangle
	index_type i0, i1, i2;

};


#endif // MESH_COMPONENTS_H