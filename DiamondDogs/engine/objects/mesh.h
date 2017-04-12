#pragma once
#ifndef VULPES_MESH_H
#define VULPES_MESH_H
#include "stdafx.h"
#include "MeshComponents.h"
#include "../renderer/objects/pipeline_object.h"
#include "../renderer/objects/uniform_buffer_object.h"

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

	SOA_Mesh() : position(0.0f), scale(1.0f, 1.0f, 1.0f), angle(0.0f) {
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

	glm::mat4 get_rte_mv(const glm::mat4& view) const;

	// VBO is actually three buffer objects, each holding seperate set of data
	vulpes::device_object<vulpes::named_buffer_t, 3> vbo;
	vulpes::device_object<vulpes::named_buffer_t> ebo;

	vulpes::device_object<vulpes::vertex_array_t> vao;

};

/*
	Specialized mesh instance for rendering relative-to-eye
*/

struct SOA_vertices_rte {

	std::vector<glm::vec3> positions_low;
	std::vector<glm::vec3> positions_high;
	std::vector<glm::vec3> normals;
	std::vector<glm::vec2> uvs;

	rte_vertex_t operator[](const GLuint& idx) const {
		return rte_vertex_t{ positions_low[idx], positions_high[idx], normals[idx], uvs[idx] };
	}

	size_t size() const {
		return positions_low.size();
	}

	GLuint add_vertex(const glm::dvec3& double_position, const glm::vec3& normal = glm::vec3(0.0f), const glm::vec2& uv = glm::vec2(0.0f)) {
		auto double_to_floats = [](const double& value)->std::pair<float, float> {
			std::pair<float, float> result;
			if (value >= 0.0) {
				double high = floorf(value / 65536.0) * 65536.0;
				result.first = (float)high;
				result.second = (float)(value - high);
			}
			else {
				double high = floorf(-value / 65536.0) * 65536.0;
				result.first = (float)-high;
				result.second = (float)(value + high);
			}
			return result;
		};
		auto xx = double_to_floats(double_position.x);
		auto yy = double_to_floats(double_position.y);
		auto zz = double_to_floats(double_position.z);
		positions_high.push_back(glm::vec3(xx.first, yy.first, zz.first));
		positions_low.push_back(glm::vec3(xx.second, yy.second, zz.second));
		normals.push_back(normal);
		uvs.push_back(uv);
		return static_cast<GLuint>(positions_high.size() - 1);
	}

	void resize(const size_t& amt) {
		positions_low.resize(amt);
		positions_high.resize(amt);
		normals.resize(amt);
		uvs.resize(amt);
	}
};

struct SOA_Mesh_rte {
	SOA_Mesh_rte() : position(0.0f), scale(1.0f, 1.0f, 1.0f), angle(0.0f) {
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

	glm::mat4 get_rte_mv(const glm::mat4& view) const;

	// VBO is actually three buffer objects, each holding seperate set of data
	vulpes::device_object<vulpes::named_buffer_t, 4> vbo;
	vulpes::device_object<vulpes::named_buffer_t> ebo;

	vulpes::device_object<vulpes::vertex_array_t> vao;
};

#endif //!VULPES_MESH_H