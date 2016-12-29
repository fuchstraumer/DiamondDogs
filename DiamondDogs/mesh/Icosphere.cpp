#include "../stdafx.h"
#include "Icosphere.h"
<<<<<<< HEAD
#include <unordered_map>

// Isochadron generation method "liberated" from Seeds of Andromeda: see blogpost 
// on gas giant generation. Raw source here: http://pastebin.com/raw/aFdWi5eQ


// Subdivides a triangle "tri" in "to", getting vertices out of "from", adding new parts back into "to" and leaving "from"
// unmodified
// DEPRECATED: Using new method below, leaving this as an example of what's happening in general.
=======

// Subdivides a triangle "tri" in "to", getting vertices out of "from", adding new parts back into "to" and leaving "from"
// unmodified
>>>>>>> a13f8af3fadbc3153e6c23b04bd9d5c84982e86b
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

<<<<<<< HEAD
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

const static float GOLDEN_RATIO = 1.61803398875f;

// Initial vertices, already normalized to unit sphere

static const std::vector<vertex_t> initialVertices = {
	vertex_t(glm::vec3(-1.0f, GOLDEN_RATIO, 0.0f)),
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

// Number of vertices and indices we start with, respectively.
const int NUM_ISOCAHEDRON_VERTICES = 12;
const int NUM_ISOCAHEDRON_INDICES = 60;

=======
>>>>>>> a13f8af3fadbc3153e6c23b04bd9d5c84982e86b
Icosphere::Icosphere(unsigned int lod_level, float radius, glm::vec3 position, glm::vec3 rotation) {
	// Set properties affecting this mesh
	Position = position;
	// We are generating a sphere: scale uniformly with magnitude given by radius.
	Scale = glm::vec3(radius);
	Angle = rotation;
	LOD_Level = lod_level;
	// Routine for generating the actual mesh
<<<<<<< HEAD
	vertLookup vertexLookup;
	// Temporary buffer for new indices
	std::vector<index_t> newIndices;
	newIndices.reserve(256);
	// Set initial vertices
	Vertices.resize(NUM_ISOCAHEDRON_VERTICES);
	for (index_t i = 0; i < NUM_ISOCAHEDRON_VERTICES; ++i) {
		Vertices[i].Position = glm::normalize(initialVertices[i].Position);
		vertexLookup[glm::normalize(initialVertices[i].Position)] = i;
	}
	// Set initial indices
	Indices.resize(NUM_ISOCAHEDRON_INDICES);
	for (index_t i = 0; i < NUM_ISOCAHEDRON_INDICES; ++i) {
		Indices[i] = initialIndices[i];
	}
	// Begin subdividing the mesh.
	for (size_t i = 0; i < static_cast<size_t>(lod_level); ++i) {
		newIndices.reserve(Indices.size() * 4);
		for (size_t j = 0; j < Indices.size(); j += 3) {
			/*
			j
			mp12   mp13
			j+1    mp23   j+2
			*/
			// Defined in counter clockwise order
			glm::vec3 vertex1 = Vertices[Indices[j + 0]].Position;
			glm::vec3 vertex2 = Vertices[Indices[j + 1]].Position;
			glm::vec3 vertex3 = Vertices[Indices[j + 2]].Position;

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
				mp12Index = static_cast<index_t>(Vertices.size());
				Vertices.push_back(vertex_t(midPoint12));
				vertexLookup[midPoint12] = mp12Index;
			}

			iter = vertexLookup.find(midPoint23);
			if (iter != vertexLookup.end()) { // It is in the map
				mp23Index = iter->second;
			}
			else { // Not in the map
				mp23Index = static_cast<index_t>(Vertices.size());
				Vertices.push_back(vertex_t(midPoint23));
				vertexLookup[midPoint23] = mp23Index;
			}

			iter = vertexLookup.find(midPoint13);
			if (iter != vertexLookup.end()) { // It is in the map
				mp13Index = iter->second;
			}
			else { // Not in the map
				mp13Index = static_cast<index_t>(Vertices.size());
				Vertices.push_back(vertex_t(midPoint13));
				vertexLookup[midPoint13] = mp13Index;
			}
			// Add our four new triangles to the mesh
			newIndices.push_back(Indices[j]);
			newIndices.push_back(mp12Index);
			newIndices.push_back(mp13Index);

			newIndices.push_back(mp12Index);
			newIndices.push_back(Indices[j + 1]);
			newIndices.push_back(mp23Index);

			newIndices.push_back(mp13Index);
			newIndices.push_back(mp23Index);
			newIndices.push_back(Indices[j + 2]);

			newIndices.push_back(mp12Index);
			newIndices.push_back(mp23Index);
			newIndices.push_back(mp13Index);
		}
		newIndices.shrink_to_fit();
		Indices.swap(newIndices);
		newIndices.clear();
	}

	for (unsigned int i = 0; i < Vertices.size(); ++i) {
		Vertices[i].Normal = glm::normalize(Vertices[i].Position - glm::vec3(0.0f));
=======
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
>>>>>>> a13f8af3fadbc3153e6c23b04bd9d5c84982e86b
	}
}
