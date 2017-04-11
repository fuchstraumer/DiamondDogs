#pragma once
#ifndef MESH_H
#define MESH_H
#include "stdafx.h"
#include "MeshComponents.h"
#include "../../engine/renderer/objects/pipeline_object.h"
#include "../../engine/renderer/objects/device_object.h"
template<typename vertex_type = vertex_t>
class Mesh {
public:

	// Default constructor/destructor.
	Mesh() = default;
	~Mesh() = default;

	Mesh(const Mesh& other) = delete;
	Mesh& operator=(const Mesh& other) = delete;

	Mesh(Mesh&& other);
	Mesh& operator=(Mesh&& other);

	// Returns references to the relevant elements
	const vertex_type& get_vertex(const GLuint& index) const;

	// Functions for adding items to the mesh. Most return indices to the relevant container

	// Add the vertex to the mesh and return it's index
	GLuint add_vertex(const vertex_type& vert);
	GLuint add_vertex(vertex_type&& vert);

	void add_triangle(const GLuint& i0, const GLuint& i1, const GLuint& i2);
	void add_triangle(GLuint&& i0, GLuint&& i1, GLuint&& i2);

	// Prepares this mesh for rendering.
	void build_render_data();

	void render(vulpes::program_pipeline_object& pipeline) const;
	

	// Updates model matrix assuming that Angle, Scale, or Position have been changed
	void UpdateModelMatrix();
	
	/*
		Members - public
	*/

	// This vector defines the position of this mesh in the world (barycentric)
	glm::vec3 position;

	// Scale of this object
	glm::vec3 scale;

	// Angle of this object relative to world axis
	glm::vec3 angle = glm::vec3(0.0f, 0.0f, 0.0f);

	// Index data storage
	std::vector<GLuint> indices;

	// Vertex data storage
	std::vector<vertex_type> vertices;

	// Matrix defining this mesh's world transformation
	glm::mat4 Model;

	// This matrix is commonly used by the fragment shader. Used to transform
	// normals to world-space coordinates.
	glm::mat4 NormTransform;

	glm::mat4 get_model_matrix(glm::vec3 position, glm::vec3 scale, glm::vec3 angle);

	vulpes::device_object<vulpes::named_buffer_t> vbo, ebo;
	vulpes::device_object<vulpes::vertex_array_t> vao;

};


__inline float maptosphere(float const &a, float const &b, float const &c) {
	float result;
	result = a * sqrt(1 - (b*b / 2.0f) - (c*c / 2.0f) + ((b*b)*(c*c) / 3.0f));
	return result;
}

template<typename vertex_type>
inline glm::mat4 Mesh<vertex_type>::get_model_matrix(glm::vec3 position, glm::vec3 scale, glm::vec3 angle){
	glm::mat4 scaleMatrix = glm::scale(glm::mat4(1.0f), scale);
	glm::mat4 translationMatrix = glm::translate(glm::mat4(1.0f), position);
	glm::mat4 rotX = glm::rotate(glm::mat4(1.0f), angle.x, glm::vec3(1.0f, 0.0f, 0.0f));
	glm::mat4 rotY = glm::rotate(glm::mat4(1.0f), angle.y, glm::vec3(0.0f, 1.0f, 0.0f));
	glm::mat4 rotZ = glm::rotate(glm::mat4(1.0f), angle.z, glm::vec3(0.0f, 0.0f, 1.0f));
	glm::mat4 result = scaleMatrix * rotX * rotY * rotZ * translationMatrix;
	return result;
};

template<typename vertex_type>
inline Mesh<vertex_type>::Mesh(Mesh && other) : vertices(std::move(other.vertices)), indices(std::move(other.indices)), Model(std::move(other.Model)), 
	normTransform(std::move(other.normTransform)), position(std::move(other.position)), angle(std::move(other.angle)), scale(std::move(other.scale)), 
	vbo(std::move(other.vbo)), ebo(std::move(other.ebo)), vao(std::move(other.vao)) {}

template<typename vertex_type>
inline GLuint Mesh<vertex_type>::add_vertex(const vertex_type & vert){
	vertices.push_back(vert);
	return static_cast<GLuint>(vertices.size() - 1);
}

template<typename vertex_type>
inline Mesh & Mesh<vertex_type>::operator=(Mesh && other)
{
	// TODO: insert return statement here
}

template<typename vertex_type>
inline GLuint Mesh<vertex_type>::add_vertex(vertex_type && vert){
	vertices.push_back(std::move(vert));
	return static_cast<GLuint>(vertices.size() - 1);
}

template<typename vertex_type>
inline void Mesh<vertex_type>::add_triangle(const GLuint & i0, const GLuint & i1, const GLuint & i2){
	indices.push_back(i0);
	indices.push_back(i1);
	indices.push_back(i2);
}

template<typename vertex_type>
inline void Mesh<vertex_type>::add_triangle(GLuint && i0, GLuint && i1, GLuint && i2){
	indices.push_back(std::move(i0));
	indices.push_back(std::move(i1));
	indices.push_back(std::move(i2));
}

template<typename vertex_type = vertex_t>
inline void Mesh<vertex_type>::build_render_data(){

	// upload data to buffers
	glNamedBufferData(vbo[0], sizeof(vertex_type) * vertices.size(), &vertices[0], GL_STATIC_DRAW);
	glNamedBufferData(ebo[0], sizeof(GLuint) * indices.size(), &indices[0], GL_STATIC_DRAW);

	// Setup attributes.
	glEnableVertexArrayAttrib(vao[0], 0);
	glEnableVertexArrayAttrib(vao[0], 1);
	glEnableVertexArrayAttrib(vao[0], 2);
	glVertexArrayVertexBuffer(vao[0], 0, vbo[0], 0, sizeof(vertex_type));
	glVertexArrayVertexBuffer(vao[0], 1, vbo[0], 0, sizeof(vertex_type));
	glVertexArrayVertexBuffer(vao[0], 2, vbo[0], 0, sizeof(vertex_type));
	glVertexArrayAttribFormat(vao[0], 0, 3, GL_FLOAT, GL_FALSE, offsetof(vertex_t, Position));
	glVertexArrayAttribFormat(vao[0], 0, 3, GL_FLOAT, GL_FALSE, offsetof(vertex_t, Normal));
	glVertexArrayAttribFormat(vao[0], 0, 3, GL_FLOAT, GL_FALSE, offsetof(vertex_t, UV));

	// bind ebo to vao
	glVertexArrayElementBuffer(vao[0], ebo[0]);

}

// Render this mesh.
template<typename vertex_type = vertex_t>
void Mesh<vertex_type>::render(vulpes::program_pipeline_object& pipeline) const {

	// Bind the VAO we will be using.
	glBindVertexArray(VAO);

	glProgramUniformMatrix4fv(pipeline.program_id, pipeline.at("model"), 1, GL_FALSE, glm::value_ptr(Model));
	if (pipeline.uniforms.count("normTransform") > 0) {
		glProgramUniformMatrix4fv(pipeline.program_id, pipeline.at("normTransform"), 1, GL_FALSE, glm::value_ptr(NormTransform));
	}

	// Draw this object using triangles and the EBO constructed earlier (glDrawElements = indexed drawing)
	glDrawElements(GL_TRIANGLES, indices.size(), GL_UNSIGNED_INT, 0);

	// Unbind the VAO since we're done drawing it.
	glBindVertexArray(0);
}

// Update the model matrix, assuming we've changed one of the attributes of this mesh
template<typename vertex_type = vertex_t>
void Mesh<vertex_type>::UpdateModelMatrix() {
	Model = buildModelMatrix(Position, Scale, Angle);
	NormTransform = glm::transpose(glm::inverse(Model));
}

#endif // !MESH_H