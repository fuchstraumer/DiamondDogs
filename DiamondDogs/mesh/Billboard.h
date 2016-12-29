#pragma once
#ifndef BILLBOARD_H
#define BILLBOARD_H
#include "../stdafx.h"
#include "../util/Shader.h"
#include "glm\gtc\matrix_transform.hpp"

class Billboard2D {

};

// Quad vertices used for the 3d billboard quad

// x,y vertex positions and UVs
GLfloat quad_vertices[] = {
	-1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
	1.0f, -1.0f, 0.0f, 1.0f, 0.0f,
	1.0f,  1.0f, 0.0f, 1.0f, 1.0f,
	1.0f,  1.0f, 0.0f, 1.0f, 1.0f,
	-1.0f,  1.0f, 0.0f, 0.0f, 1.0f,
	-1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
};


class Billboard3D {
public:
	// The exact usage of this billboard depends on which shader program is supplied
	// with the constructor.
	Billboard3D(ShaderProgram _shader) {
		shader = _shader;
	}
	~Billboard3D() = default;

	void BuildRenderData() {
		// Setup up VAO
		glGenVertexArrays(1, &VAO);
		glBindVertexArray(VAO);
		// Gen and bind main buffer object
		glGenBuffers(1, &VBO);
		glBindBuffer(GL_ARRAY_BUFFER, VBO);
		// Populate buffer with data
		glBufferData(GL_ARRAY_BUFFER, sizeof(quad_vertices), quad_vertices, GL_STATIC_DRAW);
		// Set elements/attributes of the VAO
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), (GLvoid*)0);
		glEnableVertexAttribArray(1);
		glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), (GLvoid*)sizeof(3 * sizeof(GLfloat)));
		// Set model/normtransform matrices
		glm::mat4 scale = glm::scale(glm::mat4(1.0f), Scale);
		glm::mat4 translation = glm::translate(glm::mat4(1.0f), Position);
		glm::mat4 rotX = glm::rotate(glm::mat4(1.0f), Angle.x, glm::vec3(1.0f, 0.0f, 0.0f));
		glm::mat4 rotY = glm::rotate(glm::mat4(1.0f), Angle.y, glm::vec3(0.0f, 1.0f, 0.0f));
		glm::mat4 rotZ = glm::rotate(glm::mat4(1.0f), Angle.z, glm::vec3(0.0f, 0.0f, 1.0f));
		// Final model matrix. Order of multiplication is important here (and is in general with matrix math)
		model = scale * rotX * rotY * rotZ * translation;
		// Normally, getting this element is actually fairly expensive
		normTransform = glm::transpose(glm::inverse(model));
		// Acquire locations of uniforms and set them appropriately.
		GLuint modelLoc = shader.GetUniformLocation("model");
		GLuint normTLoc = shader.GetUniformLocation("normTransform");
		glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));
		glUniformMatrix4fv(normTLoc, 1, GL_FALSE, glm::value_ptr(normTransform));
		// Unbind vertex array
		glBindVertexArray(0);
	}

	void Render() {
		shader.Use();
		glBindVertexArray(VAO);
		glDrawArrays(GL_TRIANGLES, 0, 6);
		glBindVertexArray(0);
	}

	glm::vec3 Scale, Position, Angle;
private:
	GLuint VAO, VBO;
	ShaderProgram shader;
	glm::mat4 model, normTransform;
};
#endif // !BILLBOARD_H
