#pragma once
#ifndef SKYBOX_H
#define SKYBOX_H
#include "stdafx.h"
#include "Mesh.h"
#include "util\lodeTexture.h"
class Skybox {
public:
	Skybox(const std::vector<std::string>& texture_paths) {
		std::array<glm::vec3, 8> positions{
		{   
			glm::vec3(-1.0f, -1.0f, +1.0f), // Point 0, left lower front UV{0,0}
			glm::vec3(+1.0f, -1.0f, +1.0f), // Point 1, right lower front UV{1,0}
			glm::vec3(+1.0f, +1.0f, +1.0f), // Point 2, right upper front UV{1,1}
			glm::vec3(-1.0f, +1.0f, +1.0f), // Point 3, left upper front UV{0,1}
			glm::vec3(+1.0f, -1.0f, -1.0f), // Point 4, right lower rear
			glm::vec3(-1.0f, -1.0f, -1.0f), // Point 5, left lower rear
			glm::vec3(-1.0f, +1.0f, -1.0f), // Point 6, left upper rear
			glm::vec3(+1.0f, +1.0f, -1.0f), } // Point 7, right upper rear
		};
		// Build mesh (six faces defining the cube)
		auto buildface = [this](const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3) {
			// We'll need four indices and four vertices for the two tris defining a face.
			GLuint i0, i1, i2, i3;
			vertex_t v0, v1, v2, v3;
			
			// Set the vertex positions.
			v0.Position = p0;
			v1.Position = p1;
			v2.Position = p2;
			v3.Position = p3;
			// Add the verts to the Mesh's vertex container. Returns index to added vert.
			i0 = add_vertex(std::move(v0));
			i1 = add_vertex(std::move(v1));
			i2 = add_vertex(std::move(v2));
			i3 = add_vertex(std::move(v3));
			// Add the triangles to the mesh, via indices
			add_triangle(i0, i1, i2); // Needs UVs {0,0}{1,0}{0,1}
			add_triangle(i0, i2, i3); // Needs UVs {1,0}{0,1}{1,1}
		};
		// Front
		buildface(positions[0], positions[1], positions[2], positions[3]); // Using Points 0, 1, 2, 3 and Normal 0
		// Right
		buildface(positions[1], positions[4], positions[7], positions[2]); // Using Points 1, 4, 7, 2 and Normal 1
		// Top
		buildface(positions[3], positions[2], positions[7], positions[6]); // Using Points 3, 2, 7, 6 and Normal 2
		// Left
		buildface(positions[5], positions[0], positions[3], positions[6]); // Using Points 5, 0, 3, 6 and Normal 3
		// Bottom
		buildface(positions[5], positions[4], positions[1], positions[0]); // Using Points 5, 4, 1, 0 and Normal 4
		// Back
		buildface(positions[4], positions[5], positions[6], positions[7]); // Using Points 4, 5, 6, 7 and Normal 5
		
		vulpes::compiler cl(vulpes::profile::CORE, 450);
		cl.add_shader<vulpes::vertex_shader_t>("./shaders/skybox/vertex.glsl");
		cl.add_shader<vulpes::fragment_shader_t>("./shaders/skybox/fragment.glsl");
		GLuint program_id = cl.link();
		GLbitfield stages = cl.get_program_stages();
		Program.attach_program(program_id, stages);
		Program.setup_uniforms();
		glActiveShaderProgram(Program[0], Program.program_id);
		// Setup textures
		Tex = new ldtex::CubemapTexture(texture_paths, 2048);
		Tex->BuildTexture();
	}

	void BuildRenderData(const glm::mat4& projection) {
		GLuint projLoc = Program.at("projection");
		glProgramUniformMatrix4fv(Program.program_id, projLoc, 1, GL_FALSE, glm::value_ptr(projection));
		glNamedBufferData(vbo[0], vertices.size() * sizeof(vertex_t), &vertices[0], GL_STATIC_DRAW);
		glNamedBufferData(ebo[0], indices.size() * sizeof(GLuint), &indices[0], GL_STATIC_DRAW);
		// Pointer to the position attribute of a vertex
		glEnableVertexArrayAttrib(vao[0], 0);
		glVertexArrayVertexBuffer(vao[0], 0, vbo[0], 0, sizeof(vertex_t));
		glVertexArrayAttribFormat(vao[0], 0, 3, GL_FLOAT, GL_FALSE, 0);
		glVertexArrayElementBuffer(vao[0], ebo[0]);
	}

	void Render(const glm::mat4& view) {
		glUseProgram(Program.program_id);
		glDepthFunc(GL_LEQUAL);
		glProgramUniformMatrix4fv(Program.program_id, Program.at("view"), 1, GL_FALSE, glm::value_ptr(glm::mat4(glm::mat3(view))));
		glActiveTexture(GL_TEXTURE0);
		Tex->BindTexture();
		// Change depth function to whats required to render skybox, then change it back when done.
		glBindVertexArray(vao[0]);
		glDrawElements(GL_TRIANGLES, indices.size(), GL_UNSIGNED_INT, 0);
		glBindVertexArray(0);
		glDepthFunc(GL_LESS);
		glUseProgram(0);
	}

	GLuint add_vertex(const vertex_t& v) {
		vertices.push_back(v);
		return static_cast<GLuint>(vertices.size() - 1);
	}

	void add_triangle(GLuint&& i0, GLuint&& i1, GLuint&& i2) {
		indices.push_back(std::move(i0));
		indices.push_back(std::move(i1));
		indices.push_back(std::move(i2));
	}

	void add_triangle(const GLuint& i0, const GLuint& i1, const GLuint& i2) {
		indices.push_back(i0);
		indices.push_back(i1);
		indices.push_back(i2);
	}

	vulpes::device_object<vulpes::named_buffer_t> vbo, ebo;
	vulpes::device_object<vulpes::vertex_array_t> vao;
	vulpes::program_pipeline_object Program;
	std::vector<vertex_t> vertices;
	std::vector<GLuint> indices;
	ldtex::CubemapTexture* Tex;
};

#endif // !SKYBOX_H
