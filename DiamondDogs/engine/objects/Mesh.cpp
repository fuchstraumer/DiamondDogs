#include "stdafx.h"
#include "Mesh.h"

vertex_t SOA_Mesh::get_vertex(const GLuint & idx) const{
	return vertices[idx];
}

GLuint SOA_Mesh::add_vertex(const vertex_t & v){
	vertices.positions.push_back(v.Position);
	vertices.normals.push_back(v.Normal);
	vertices.uvs.push_back(v.UV);
	return static_cast<GLuint>(vertices.positions.size() - 1);
}

GLuint SOA_Mesh::add_vertex(vertex_t && v){
	vertices.positions.push_back(std::move(v.Position));
	vertices.normals.push_back(std::move(v.Normal));
	vertices.uvs.push_back(std::move(v.UV));
	return static_cast<GLuint>(vertices.positions.size() - 1);
}

void SOA_Mesh::add_triangle(const GLuint & i0, const GLuint & i1, const GLuint & i2){
	indices.push_back(i0);
	indices.push_back(i1);
	indices.push_back(i2);
}

void SOA_Mesh::build_render_data(const vulpes::program_pipeline_object& shader, const glm::mat4 & projection){
	glProgramUniformMatrix4fv(shader.program_id, shader.at("model"), 1, GL_FALSE, glm::value_ptr(get_model_matrix()));
	glProgramUniformMatrix4fv(shader.program_id, shader.at("projection"), 1, GL_FALSE, glm::value_ptr(projection));

	// Upload data to vbo's and perform setup.
	glNamedBufferData(vbo[0], sizeof(glm::vec3) * vertices.size(), &vertices.positions[0], GL_DYNAMIC_DRAW);
	glEnableVertexArrayAttrib(vao[0], 0);
	glVertexArrayVertexBuffer(vao[0], 0, vbo[0], 0, sizeof(glm::vec3));
	glVertexArrayAttribFormat(vao[0], 0, 3, GL_FLOAT, GL_FALSE, 0);

	glNamedBufferData(vbo[1], sizeof(glm::vec3) * vertices.size(), &vertices.normals[0], GL_STATIC_DRAW);
	glEnableVertexArrayAttrib(vao[0], 1);
	glVertexArrayVertexBuffer(vao[0], 1, vbo[1], 0, sizeof(glm::vec3));
	glVertexArrayAttribFormat(vao[0], 1, 3, GL_FLOAT, GL_FALSE, 0);

	glNamedBufferData(vbo[2], sizeof(glm::vec2) * vertices.size(), &vertices.uvs[0], GL_STATIC_DRAW);
	glEnableVertexArrayAttrib(vao[0], 2);
	glVertexArrayVertexBuffer(vao[0], 2, vbo[2], 0, sizeof(glm::vec2));
	glVertexArrayAttribFormat(vao[0], 2, 2, GL_FLOAT, GL_FALSE, 0);


	glNamedBufferData(ebo[0], sizeof(GLuint) * indices.size(), &indices[0], GL_STATIC_DRAW);
	glVertexArrayElementBuffer(vao[0], ebo[0]);
}

void SOA_Mesh::render(const vulpes::program_pipeline_object & shader, const glm::mat4& view){
	glUseProgram(shader.program_id);
	glBindVertexArray(vao[0]);
	glProgramUniformMatrix4fv(shader.program_id, shader.at("view"), 1, GL_FALSE, glm::value_ptr(view));
	if (shader.uniforms.count("normTransform") > 0) {
		// glProgramUniformMatrix4fv(pipeline.program_id, pipeline.at("normTransform"), 1, GL_FALSE, glm::value_ptr(NormTransform));
	}

	glDrawElements(GL_TRIANGLES, indices.size(), GL_UNSIGNED_INT, 0);
}

glm::mat4 SOA_Mesh::get_model_matrix() const{
	glm::mat4 scaleMatrix = glm::scale(glm::mat4(1.0f), scale);
	glm::mat4 translationMatrix = glm::translate(glm::mat4(1.0f), position);
	glm::mat4 rotX = glm::rotate(glm::mat4(1.0f), angle.x, glm::vec3(1.0f, 0.0f, 0.0f));
	glm::mat4 rotY = glm::rotate(glm::mat4(1.0f), angle.y, glm::vec3(0.0f, 1.0f, 0.0f));
	glm::mat4 rotZ = glm::rotate(glm::mat4(1.0f), angle.z, glm::vec3(0.0f, 0.0f, 1.0f));
	glm::mat4 result = scaleMatrix * rotX * rotY * rotZ * translationMatrix;
	return result;
}
