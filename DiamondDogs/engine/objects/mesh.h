#pragma once
#ifndef VULPES_MESH_H
#define VULPES_MESH_H
#include "stdafx.h"
#include "MeshComponents.h"
#include "../renderer/objects/pipeline_object.h"

struct SOA_Vertices {
	std::vector<glm::vec3> positions;
	std::vector<glm::vec3> normals;
	std::vector<glm::vec2> uvs;
	vertex_t operator[](const GLuint& idx) const {
		return vertex_t{ positions[idx], normals[idx], uvs[idx] };
	}
	size_t size() const {
		return positions.size();
	}
	void resize(const size_t& amt) {
		positions.resize(amt);
		normals.resize(amt);
		uvs.resize(amt);
	}
};

struct SOA_Mesh {

	SOA_Mesh() : position(0.0f), scale(1.0f), angle(0.0f) {
		model = get_model_matrix();
	}
	
	SOA_Vertices vertices;

	std::vector<GLuint> indices;

	

	vertex_t get_vertex(const GLuint& idx) const;

	GLuint add_vertex(const vertex_t& v);
	GLuint add_vertex(vertex_t&& v);

	void add_triangle(const GLuint& i0, const GLuint& i1, const GLuint& i2);

	void build_render_data(const vulpes::program_pipeline_object& shader, const glm::mat4& projection);
	void render(const vulpes::program_pipeline_object& shader, const glm::mat4& view);

	glm::mat4 model;
	glm::vec3 position, scale, angle;

	glm::mat4 get_model_matrix() const;

	// VBO is actually three buffer objects, each holding seperate set of data
	vulpes::device_object<vulpes::named_buffer_t, 3> vbo;
	vulpes::device_object<vulpes::named_buffer_t> ebo;

	vulpes::device_object<vulpes::vertex_array_t> vao;

};

#endif //!VULPES_MESH_H