#pragma once
#ifndef MESH_COMPONENTS_H
#define MESH_COMPONENTS_H
#include "../stdafx.h"
#include <unordered_map>
#include <unordered_set>
/*
	MESH_COMPONENTS_H

	This header contains definitions and implementations for the Mesh
	components. This includes:

	vertex_t - vertex type
	index_t - index type
	tri_t - triangle type
	face_t - face type
	EdgeLookup - templated version of an unordered map, 
	using a custom hashing function. Stores pairs of indices
	that form edges

*/

// Index type
using index_t = std::uint32_t;

// Unordered map does not have hash function defined for a std::pair
// of indices, need to supply our own. Wrapping in struct makes 
// passing it to the map easier.
struct HashIndex {
	size_t operator()(const std::pair<index_t, index_t> &x) const {
		size_t res;
		res = ((x.first >> 16) ^ x.second) * 0x45d0f3b;
		res = ((x.second >> 16) ^ x.first) * 0x45d9f3b;
		return res;
	}
};

// Vertex struct
using vertex_t = class vert {
public:
	vert(glm::vec3 pos) {
		this->Position = pos;
	}
	vert(glm::vec3 pos, glm::vec3 norm) {
		this->Position = pos;
		this->Normal = norm;
	}
	vert() { }
	glm::vec3 Position;
	glm::vec3 Normal;
	glm::vec2 UV;
};

// Get vertex with position in between the two input vertices
inline static vert getMiddlePoint(vert const &v0, vert const &v1) {
	vert result;
	result.Position = (v0.Position + v1.Position) / 2.0f;
	result.Normal = glm::normalize(v0.Normal - v1.Normal);
	return result;
}


// Create an edge and add it, return to call
using edge_key = std::pair<index_t, index_t>;

// Returns an edge_key (or just the edge itself) based on the vertex indices i0 and i1
inline edge_key CreateEdge(index_t i0, index_t i1) {
	edge_key newKey(i0, i1);
	if (newKey.first > newKey.second) {
		std::swap(newKey.first, newKey.second);
	}
	return newKey;
}

// Edge struct: contains the key that defines the actual mesh, along with index to parent
using edge_t = struct edge {
	edge_key Key;
	index_t ParentIndex;

	edge(index_t const &i0, index_t const &i1, index_t const &triIndex = 0) {
		Key = CreateEdge(i0, i1);
		ParentIndex = triIndex;
	}

	edge() { }

	const index_t& start() {
		return Key.first;
	}

	const index_t& end() {
		return Key.second;
	}

};

// Triangle struct - three vertices that make up the base drawn type
using triangle_t = struct tri {

	// Create a triangle using the three vertex indices giving.
	tri(index_t const &i0, index_t const &i1, index_t const &i2) {
		this->i0 = i0;
		this->i1 = i1;
		this->i2 = i2;
		MakeEdges();
	}

	// Default empty constructor
	tri() { }

	// Vertex indices for this triangle
	index_t i0, i1, i2;

	// Edges making up this triangle.
	edge_key e0, e1, e2;

	// Make the edge_keys for this triangle.
	void MakeEdges() {
		e0 = CreateEdge(i0, i1);
		e1 = CreateEdge(i1, i2);
		e2 = CreateEdge(i2, i0);
	}
};

// Edge class is part of the mesh: the key value is how the edge is stored in the edge map.
// The key is the two indices to the vertices defining this edge.
using EdgeLookup = std::unordered_map<std::pair<index_t, index_t>, index_t, HashIndex>;

#endif // MESH_COMPONENTS_H