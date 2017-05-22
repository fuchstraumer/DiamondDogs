#include "stdafx.h"
#include "Icosphere.h"
#include <unordered_map>

// Isochadron generation method "liberated" from Seeds of Andromeda: see blogpost 
// on gas giant generation. Raw source here: http://pastebin.com/raw/aFdWi5eQ

inline vertex_t get_middle_vertex(const vertex_t& v0, const vertex_t& v1) {
	vertex_t res;
	res.Position = (v0.Position + v1.Position) / 2.0f;
	res.Normal = glm::normalize(res.Position - glm::vec3(0.0f));
	return res;
}

// Functions required to add a glm::vec3 into an unordered map: hashing functions, essentially
struct vecHash {
	// Hashing function, required for using an unordered map and being able to retrieve values
	// The standard library does not include hashing functions for MANY types. This is an example
	// of how to create a hashing function: it should carry over to many other sorts.
	size_t operator()(const glm::vec3& v) const {
		return std::hash<float>()(v.x) ^ std::hash<float>()(v.y) ^ std::hash<float>()(v.z);
	}
	// Function used to check if entry already exists in map
	bool operator()(const glm::vec3& v0, const glm::vec3& v1) const {
		return (v0.x == v1.x && v0.y == v1.y && v0.z == v1.z);
	}
};

// Alias declaration for the lookup object.
// First entry in template is the position, second is the index to vert, next two are 
// required functions. Note that they use the () operator, but are distinguished by 
// given parameters and return type. See reference material on unordered_map for more
using vertLookup = std::unordered_map<glm::vec3, index_t, vecHash, vecHash>;

// Simple inline method for getting midpoint position using two input positions
inline glm::vec3 findMidpoint(glm::vec3 vertex1, glm::vec3 vertex2) {
	return glm::normalize(glm::vec3((vertex1.x + vertex2.x) / 2.0f, (vertex1.y + vertex2.y) / 2.0f, (vertex1.z + vertex2.z) / 2.0f));
}

// Constants used to generate the icosphere: key numeric constants, along with 12 initial vertices + triangles

constexpr float GOLDEN_RATIO = 1.61803398875f;

// Initial vertices, already normalized to unit sphere

static const std::array<vertex_t, 12> initialVertices = {
	vertex_t{glm::vec3(-1.0f, GOLDEN_RATIO, 0.0f)},
	vertex_t(glm::vec3(1.0f, GOLDEN_RATIO, 0.0f)),
	vertex_t(glm::vec3(-1.0f, -GOLDEN_RATIO, 0.0f)),
	vertex_t(glm::vec3(1.0f, -GOLDEN_RATIO, 0.0f)),

	vertex_t(glm::vec3(0.0f, -1.0f, GOLDEN_RATIO)),
	vertex_t(glm::vec3(0.0f, 1.0f, GOLDEN_RATIO)),
	vertex_t(glm::vec3(0.0f, -1.0, -GOLDEN_RATIO)),
	vertex_t(glm::vec3(0.0f, 1.0f, -GOLDEN_RATIO)),

	vertex_t(glm::vec3(GOLDEN_RATIO, 0.0f, -1.0f)),
	vertex_t(glm::vec3(GOLDEN_RATIO, 0.0f, 1.0f)),
	vertex_t(glm::vec3(-GOLDEN_RATIO, 0.0f, -1.0f)),
	vertex_t(glm::vec3(-GOLDEN_RATIO, 0.0, 1.0f)),
};

// Initial triangles, with indices referring to the above container
static const std::vector<index_t> initialIndices = {
	0, 11, 5,
	0, 5, 1,
	0, 1, 7,
	0, 7, 10,
	0, 10, 11,

	1, 5, 9,
	5, 11, 4,
	11, 10, 2,
	10, 7, 6,
	7, 1, 8,

	3, 9, 4,
	3, 4, 2,
	3, 2, 6,
	3, 6, 8,
	3, 8, 9,

	4, 9, 5,
	2, 4, 11,
	6, 2, 10,
	8, 6, 7,
	9, 8, 1
};

namespace vulpes {

	// Number of vertices and indices we start with, respectively.
	const int NUM_ISOCAHEDRON_VERTICES = 12;
	const int NUM_ISOCAHEDRON_INDICES = 60;

	Icosphere::Icosphere(Icosphere && other) noexcept : LOD_Level(std::move(other.LOD_Level)) {}

	Icosphere & Icosphere::operator=(Icosphere && other) noexcept {
		LOD_Level = std::move(other.LOD_Level);
		return *this;
	}

	Icosphere::Icosphere(unsigned int lod_level, float radius, glm::vec3 _position, glm::vec3 rotation) {
		// Set properties affecting this mesh
		position = _position;
		// We are generating a sphere: scale uniformly with magnitude given by radius.
		scale = glm::vec3(radius);
		angle = rotation;
		LOD_Level = lod_level;
		// Routine for generating the actual mesh
		vertLookup vertexLookup;
		// Temporary buffer for new indices
		std::vector<index_t> newIndices;
		newIndices.reserve(256);
		// Set initial vertices
		vertices.resize(NUM_ISOCAHEDRON_VERTICES);
		for (index_t i = 0; i < NUM_ISOCAHEDRON_VERTICES; ++i) {
			vertices.positions[i] = glm::normalize(initialVertices[i].Position);
			vertexLookup[glm::normalize(initialVertices[i].Position)] = i;
		}
		// Set initial indices
		indices.resize(NUM_ISOCAHEDRON_INDICES);
		for (index_t i = 0; i < NUM_ISOCAHEDRON_INDICES; ++i) {
			indices[i] = initialIndices[i];
		}
		// Begin subdividing the mesh.
		for (size_t i = 0; i < static_cast<size_t>(lod_level); ++i) {
			newIndices.reserve(indices.size() * 4);
			for (size_t j = 0; j < indices.size(); j += 3) {
				/*
				j
				mp12   mp13
				j+1    mp23   j+2
				*/
				// Defined in counter clockwise order
				const glm::vec3& vertex1 = vertices.positions[indices[j + 0]];
				const glm::vec3& vertex2 = vertices.positions[indices[j + 1]];
				const glm::vec3& vertex3 = vertices.positions[indices[j + 2]];

				glm::vec3 midPoint12 = findMidpoint(vertex1, vertex2);
				glm::vec3 midPoint23 = findMidpoint(vertex2, vertex3);
				glm::vec3 midPoint13 = findMidpoint(vertex1, vertex3);

				uint32_t mp12Index;
				uint32_t mp23Index;
				uint32_t mp13Index;

				auto iter = vertexLookup.find(midPoint12);
				if (iter != vertexLookup.end()) { // It is in the map
					mp12Index = iter->second;
				}
				else { // Not in the map
					mp12Index = add_vertex(vertex_t(midPoint12));
					vertexLookup[midPoint12] = mp12Index;
				}

				iter = vertexLookup.find(midPoint23);
				if (iter != vertexLookup.end()) { // It is in the map
					mp23Index = iter->second;
				}
				else { // Not in the map
					mp23Index = add_vertex(vertex_t(midPoint23));
					vertexLookup[midPoint23] = mp23Index;
				}

				iter = vertexLookup.find(midPoint13);
				if (iter != vertexLookup.end()) { // It is in the map
					mp13Index = iter->second;
				}
				else { // Not in the map
					mp13Index = add_vertex(vertex_t(midPoint13));
					vertexLookup[midPoint13] = mp13Index;
				}
				// Add our four new triangles to the mesh
				newIndices.push_back(indices[j]);
				newIndices.push_back(mp12Index);
				newIndices.push_back(mp13Index);

				newIndices.push_back(mp12Index);
				newIndices.push_back(indices[j + 1]);
				newIndices.push_back(mp23Index);

				newIndices.push_back(mp13Index);
				newIndices.push_back(mp23Index);
				newIndices.push_back(indices[j + 2]);

				newIndices.push_back(mp12Index);
				newIndices.push_back(mp23Index);
				newIndices.push_back(mp13Index);
			}
			newIndices.shrink_to_fit();
			indices.swap(newIndices);
			newIndices.clear();
		}

		for (unsigned int i = 0; i < vertices.size(); ++i) {
			vertices.normals_uvs[i].normal = glm::normalize(vertices.positions[i] - glm::vec3(0.0f));
		}
	}
}