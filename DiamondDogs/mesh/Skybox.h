#pragma once
#ifndef SKYBOX_H
#define SKYBOX_H
#include "../stdafx.h"
#include "Mesh.h"
#include "../util//Shader.h"
#include <array>
class Skybox : public Mesh {
public:
	Skybox() : Mesh() {
		std::array<glm::vec3, 8> vertices{
			{ glm::vec3(-1.0f, -1.0f, +1.0f), // Point 0, left lower front UV{0,0}
			glm::vec3(+1.0f, -1.0f, +1.0f), // Point 1, right lower front UV{1,0}
			glm::vec3(+1.0f, +1.0f, +1.0f), // Point 2, right upper front UV{1,1}
			glm::vec3(-1.0f, +1.0f, +1.0f), // Point 3, left upper front UV{0,1}
			glm::vec3(+1.0f, -1.0f, -1.0f), // Point 4, right lower rear
			glm::vec3(-1.0f, -1.0f, -1.0f), // Point 5, left lower rear
			glm::vec3(-1.0f, +1.0f, -1.0f), // Point 6, left upper rear
			glm::vec3(+1.0f, +1.0f, -1.0f), } // Point 7, right upper rear
		};
		// Build mesh (six faces defining the cube)
		auto buildface = [this](glm::vec3 p0, glm::vec3 p1, glm::vec3 p2, glm::vec3 p3) {
			// We'll need four indices and four vertices for the two tris defining a face.
			index_t i0, i1, i2, i3;
			vertex_t v0, v1, v2, v3;
			// Set the vertex positions.
			v0.Position = p0;
			v1.Position = p1;
			v2.Position = p2;
			v3.Position = p3;
			// Set vertex normals.
			// Add the verts to the Mesh's vertex container. Returns index to added vert.
			i0 = AddVert(v0);
			i1 = AddVert(v1);
			i2 = AddVert(v2);
			i3 = AddVert(v3);
			// Add the triangles to the mesh, via indices
			face_t newFace = CreateFace(i0, i1, i2, i3);
			AddFace(newFace);
		};
		// Front
		buildface(vertices[0], vertices[1], vertices[2], vertices[3]); // Using Points 0, 1, 2, 3 and Normal 0
		// Right
		buildface(vertices[1], vertices[4], vertices[7], vertices[2]); // Using Points 1, 4, 7, 2 and Normal 1
		// Top
		buildface(vertices[3], vertices[2], vertices[7], vertices[6]); // Using Points 3, 2, 7, 6 and Normal 2
		// Left
		buildface(vertices[5], vertices[0], vertices[3], vertices[6]); // Using Points 5, 0, 3, 6 and Normal 3
		// Bottom
		buildface(vertices[5], vertices[4], vertices[1], vertices[0]); // Using Points 5, 4, 1, 0 and Normal 4
		// Back
		buildface(vertices[4], vertices[5], vertices[6], vertices[7]); // Using Points 4, 5, 6, 7 and Normal 5
		

	}

	void BuildRenderData() {
		glGenVertexArrays(1, &VAO);
		glGenBuffers(1, &VBO); glGenBuffers(1, &EBO);
		glBindVertexArray(VAO);
		// Bind the vertex buffer and then specify what data it will be loaded with
		glBindBuffer(GL_ARRAY_BUFFER, VBO);
		glBufferData(GL_ARRAY_BUFFER, GetNumVerts() * sizeof(vertex_t), &(Vertices[0]), GL_DYNAMIC_DRAW);
		// Bind the element array (indice) buffer and fill it with data
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, GetNumIndices() * sizeof(index_t), &(Indices.front()), GL_DYNAMIC_DRAW);
		// Pointer to the position attribute of a vertex
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(vertex_t), (GLvoid*)0);
		glBindVertexArray(0);
	}

	void RenderSkybox(ShaderProgram& shader, glm::mat4 view_matrix, glm::mat4 projection_matrix) {
		shader.Use();
		GLuint viewLoc = glGetUniformLocation(shader.Handle, "view");
		GLuint projLoc = glGetUniformLocation(shader.Handle, "projection");
		glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view_matrix));
		glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(projection_matrix));
		glBindVertexArray(VAO);
		glDrawElements(GL_TRIANGLES, GetNumIndices(), GL_UNSIGNED_INT, 0);
		glBindVertexArray(0);
	}

	GLuint VAO, VBO, EBO;
};

#endif // !SKYBOX_H
