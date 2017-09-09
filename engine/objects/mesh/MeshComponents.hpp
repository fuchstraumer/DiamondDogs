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

struct rte_vertex_t {
	rte_vertex_t(const glm::dvec3& pos, const glm::vec3& _normal = glm::vec3(0.0f), const glm::vec2& _uv = glm::vec2(0.0f)) : normal(_normal), uv(_uv) {
		auto double_to_floats = [](const double& value)->std::pair<float, float> {
			std::pair<float, float> result;
			if (value >= 0.0) {
				double high = floor(value / 65536.0) * 65536.0;
				result.first = (float)high;
				result.second = (float)(value - high);
			}
			else {
				double high = floor(-value / 65536.0) * 65536.0;
				result.first = (float)-high;
				result.second = (float)(value + high);
			}
			return result;
		};
		auto xx = double_to_floats(pos.x);
		auto yy = double_to_floats(pos.y);
		auto zz = double_to_floats(pos.z);
		position_high = glm::vec3(xx.first, yy.first, zz.first);
		position_low = glm::vec3(xx.second, yy.second, zz.second);
	}
	glm::vec3 position_low, position_high;
	glm::vec3 normal;
	glm::vec2 uv;
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