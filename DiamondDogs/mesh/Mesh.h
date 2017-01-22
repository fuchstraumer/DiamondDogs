#pragma once
#ifndef MESH_H
#define MESH_H
#include "stdafx.h"
#include "../util/Shader.h"
#include "MeshComponents.h"

template<typename vertex_type = vertex_t, typename index_type = index_t>
class Mesh {
public:

	// Default constructor/destructor.
	Mesh() = default;
	~Mesh() = default;
	
	// Returns number of indices in this mesh
	GLsizei GetNumIndices(void) const;
	// Returns number of vertices in this mesh
	GLsizei GetNumVerts(void) const;

	// Returns references to the relevant elements
	const vertex_type& GetVertex(index_type  index) const;
	const index_type & GetIndex(index_type  index) const;
	const triangle_t<index_type>& GetTri(index_type t_index) const;

	// Functions for adding items to the mesh. Most return indices to the relevant container

	// Add the vertex to the mesh and return it's index
	index_type  AddVert(const vertex_type& vert);

	// Add triangle to the mesh, using three indices specified, return index
	index_type  AddTriangle(const index_type &i0, const index_type &i1, const index_type &i2);

	// Add a triangle to the mesh useing a triangle reference.
	index_type  AddTriangle(const triangle_t<index_type> & tri);

	// Maps an input vertex to a sphere
	vertex_type VertToSphere(vertex_type in, float radius) const;

	// Maps an input vertex to a unit sphere
	vertex_type VertToUnitSphere(const vertex_type& in) const;

	glm::vec3 PointToUnitSphere(const glm::vec3 & in) const;

	// Get the vertex in between the two vertices given by i0 and i1
	vertex_type GetMiddlePoint(const index_type  & i0, const index_type  & i1) const;

	// Get the vertex in between the input vertices v0 and v1
	vertex_type GetMiddlePoint(const vertex_type& v0, const vertex_type& v1) const;

	// Prepares this mesh for rendering.
	void BuildRenderData();

	// Renders this object using the given shader program
	void Render(ShaderProgram& shader) const;

	// Updates model matrix assuming that Angle, Scale, or Position have been changed
	void UpdateModelMatrix();
	
	/*
		Members - public
	*/

	// This vector defines the position of this mesh in the world (barycentric)
	glm::vec3 Position;

	// Scale of this object
	glm::vec3 Scale;

	// Angle of this object relative to world axis
	glm::vec3 Angle = glm::vec3(0.0f, 0.0f, 0.0f);

	// Index data storage
	std::vector<index_type> Indices;

	// Vertex data storage
	std::vector<vertex_type> Vertices;

	// Triangle/face data storage
	std::vector<triangle_t<index_type>> Triangles;

private:
	/*
		Members - private
	*/

	// Tells us whether or not this object is ready to render
	bool meshBuilt = false;

	// Used to access GL buffer/array objects
	GLuint VAO, VBO, EBO;

	// Matrix defining this mesh's world transformation
	glm::mat4 Model;

	// This matrix is commonly used by the fragment shader. Used to transform
	// normals to world-space coordinates.
	glm::mat4 NormTransform;

	glm::mat4 buildModelMatrix(glm::vec3 position, glm::vec3 scale, glm::vec3 angle);

};


__inline float maptosphere(float const &a, float const &b, float const &c) {
	float result;
	result = a * sqrt(1 - (b*b / 2.0f) - (c*c / 2.0f) + ((b*b)*(c*c) / 3.0f));
	return result;
}

template<typename vertex_type, typename index_type>
inline glm::mat4 Mesh<vertex_type, index_type>::buildModelMatrix(glm::vec3 position, glm::vec3 scale, glm::vec3 angle){
	glm::mat4 scaleMatrix = glm::scale(glm::mat4(1.0f), scale);
	glm::mat4 translationMatrix = glm::translate(glm::mat4(1.0f), position);
	glm::mat4 rotX = glm::rotate(glm::mat4(1.0f), angle.x, glm::vec3(1.0f, 0.0f, 0.0f));
	glm::mat4 rotY = glm::rotate(glm::mat4(1.0f), angle.y, glm::vec3(0.0f, 1.0f, 0.0f));
	glm::mat4 rotZ = glm::rotate(glm::mat4(1.0f), angle.z, glm::vec3(0.0f, 0.0f, 1.0f));
	glm::mat4 result = scaleMatrix * rotX * rotY * rotZ * translationMatrix;
	return result;
};

// Return number of indices in this mesh
template<typename vertex_type = vertex_t, typename index_type = index_t>
GLsizei Mesh<vertex_type, index_type>::GetNumIndices() const {
	return static_cast<GLsizei>(Indices.size());
}

// Return number of verts in this mesh
template<typename vertex_type = vertex_t, typename index_type = index_t>
GLsizei Mesh<vertex_type, index_type>::GetNumVerts() const {
	return static_cast<GLsizei>(Vertices.size());
}

// Get vertex reference at index
template<typename vertex_type = vertex_t, typename index_type = index_t>
const vertex_type& Mesh<vertex_type, index_type>::GetVertex(index_type index) const {
	return Vertices[index];
}

// Get index reference at index
template<typename vertex_type = vertex_t, typename index_type = index_t>
const index_type& Mesh<vertex_type, index_type>::GetIndex(index_type index) const {
	return Indices[index];
}

// Get tri at index and return a reference
template<typename vertex_type = vertex_t, typename index_type = index_t>
const triangle_t<index_type>& Mesh<vertex_type, index_type>::GetTri(index_type t_index) const{
	return Triangles[t_index];
}

// Add vert and return index to it
template<typename vertex_type = vertex_t, typename index_type = index_t>
index_type Mesh<vertex_type, index_type>::AddVert(const vertex_type & vert) {
	Vertices.push_back(vert);
	return static_cast<index_type>(Vertices.size() - 1);
}

// Add triangle using three indices and return a reference to this triangle
template<typename vertex_type = vertex_t, typename index_type = index_t>
index_type Mesh<vertex_type, index_type>::AddTriangle(const index_type &i0, const index_type &i1, const index_type &i2) {
	Indices.push_back(i0);
	Indices.push_back(i1);
	Indices.push_back(i2);
	triangle_t<index_type> newTri(i0, i1, i2);
	Triangles.push_back(std::move(newTri));
	index_type val = static_cast<index_type>(Triangles.size() - 1);
	return val;
}

template<typename vertex_type = vertex_t, typename index_type = index_t>
index_type Mesh<vertex_type, index_type>::AddTriangle(const triangle_t<index_type> &tri) {
	Indices.push_back(tri.i0);
	Indices.push_back(tri.i1);
	Indices.push_back(tri.i2);
	Triangles.push_back(tri);
	return static_cast<index_t>(Triangles.size() - 1);
}

template<typename vertex_type = vertex_t, typename index_type = index_t>
vertex_type Mesh<vertex_type, index_type>::VertToSphere(vertex_type in, float radius) const {
	vertex_type result;
	in.Position = glm::normalize(in.Position);
	result.Normal = in.Position - glm::vec3(0.0f);
	result.Position = glm::normalize(result.Normal * radius);
	return result;
}

template<typename vertex_type = vertex_t, typename index_type = index_t>
vertex_type Mesh<vertex_type, index_type>::VertToUnitSphere(const vertex_type & in) const {
	vertex_type result;
	result.Position = glm::normalize(in.Position);
	result.Normal = glm::normalize(result.Position - glm::vec3(0.0f));
	return result;
}

template<typename vertex_type = vertex_t, typename index_type = index_t>
glm::vec3 Mesh<vertex_type, index_type>::PointToUnitSphere(const glm::vec3 &in) const {
	glm::vec3 res;
	auto coordToSphere = [](float a, float b, float c)->float {
		float lambda;
		float bSq, cSq;
		bSq = b*b;
		cSq = c*c;
		lambda = std::sqrtf(1.0f - (bSq / 2.0f) - (cSq / 2.0f) + ((bSq*cSq) / 3.0f));
		return a * lambda;
	};
	res.x = coordToSphere(in.x, in.y, in.z);
	res.y = coordToSphere(in.y, in.z, in.x);
	res.z = coordToSphere(in.z, in.x, in.y);
	return res;
}

template<typename vertex_type = vertex_t, typename index_type = index_t>
vertex_type Mesh<vertex_type, index_type>::GetMiddlePoint(const index_type &i0, const index_type &i1) const {
	vertex_type res;
	auto&& v0 = GetVertex(i0);
	auto&& v1 = GetVertex(i1);
	res.Position = (v0.Position + v1.Position) / 2.0f;
	return res;
}

template<typename vertex_type = vertex_t, typename index_type = index_t>
vertex_type Mesh<vertex_type, index_type>::GetMiddlePoint(const vertex_type &v0, const vertex_type &v1) const {
	vertex_type res;
	res.Position = (v0.Position + v1.Position) / 2.0f;
	res.Normal = glm::normalize(res.Position - glm::vec3(0.0f));
	return res;
}

template<typename vertex_type = vertex_t, typename index_type = index_t>
void Mesh<vertex_type, index_type>::BuildRenderData() {

	// Generate the VAO and buffers for this mesh.
	glGenVertexArrays(1, &VAO);
	glGenBuffers(1, &VBO); glGenBuffers(1, &EBO);

	// Bind the VAO and begin setting things up.
	glBindVertexArray(VAO);

	// Bind the vertex buffer and fill it with data
	glBindBuffer(GL_ARRAY_BUFFER, VBO);
	glBufferData(GL_ARRAY_BUFFER, GetNumVerts() * sizeof(vertex_t), &(Vertices[0]), GL_STATIC_DRAW);

	// Bind the element array (indice) buffer and fill it with data
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, GetNumIndices() * sizeof(index_t), &(Indices[0]), GL_STATIC_DRAW);

	// Pointer to the position attribute of a vertex
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(vertex_t), (GLvoid*)0);
	glEnableVertexAttribArray(0);

	// Pointer to the normal of a vertex
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(vertex_t), (GLvoid*)offsetof(vertex_type, Normal));
	glEnableVertexAttribArray(1);

	// Texture coordinate of a vertex
	glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(vertex_t), (GLvoid*)offsetof(vertex_type, UV));
	glEnableVertexAttribArray(2);

	// Setup model matrix.
	Model = buildModelMatrix(Position, Scale, Angle);

	// Set transformation matrix for transforming normal to world-space,
	// used to pass a correct normal to the fragment shader. Cheaper to precalculate here
	// then calculate each frame on the GPU
	NormTransform = glm::transpose(glm::inverse(Model));

	// Unbind the VAO since we're done with it.
	glBindVertexArray(0);
	meshBuilt = true;
}

// Render this mesh.
template<typename vertex_type = vertex_t, typename index_type = index_t>
void Mesh<vertex_type, index_type>::Render(ShaderProgram & shader) const {
	// Activate shader passed in.
	shader.Use();

	// Bind the VAO we will be using.
	glBindVertexArray(VAO);

	// Get the location of the model from the shader's map object, and set the model matrix in the shader
	GLint modelLoc = shader.GetUniformLocation("model");
	glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(Model));

	// Perform the same process for the normal transformation matrix.
	GLint normTLoc = shader.GetUniformLocation("normTransform");
	glUniformMatrix4fv(normTLoc, 1, GL_FALSE, glm::value_ptr(NormTransform));

	// Draw this object using triangles and the EBO constructed earlier (glDrawElements = indexed drawing)
	glDrawElements(GL_TRIANGLES, GetNumIndices(), GL_UNSIGNED_INT, 0);

	// Unbind the VAO since we're done drawing it.
	glBindVertexArray(0);
}

// Update the model matrix, assuming we've changed one of the attributes of this mesh
template<typename vertex_type = vertex_t, typename index_type = index_t>
void Mesh<vertex_type, index_type>::UpdateModelMatrix() {
	Model = buildModelMatrix(Position, Scale, Angle);
	NormTransform = glm::transpose(glm::inverse(Model));
}

#endif // !MESH_H