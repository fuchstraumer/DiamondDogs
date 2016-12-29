#pragma once
#ifndef BILLBOARD_H
#define BILLBOARD_H
#include "../stdafx.h"
#include "../util/Shader.h"
#include "glm\gtc\matrix_transform.hpp"

// Vector pointing up, constant since in most cases we don't want billboard rotating.
static const glm::vec3 WORLD_UP = glm::vec3(0.0f, 1.0f, 0.0f);

class Billboard2D {

};

// Quad vertices used for the 3d billboard quad

// x,y vertex positions and UVs
static const GLfloat quad_vertices[] = {
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
	Billboard3D(ShaderProgram& _shader) : shader(_shader) {

	}
	~Billboard3D() = default;

	Billboard3D &Billboard3D::operator =(const Billboard3D &other) {
		this->shader = other.shader;
		return *this;
	}

	void BuildRenderData(const glm::mat4& projection) {
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

		GLuint projLoc = shader.GetUniformLocation("projection");
		glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(projection));
		glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));

		// Unbind vertex array
		glBindVertexArray(0);
	}

	void Render(const glm::mat4& view, const glm::vec3& camera_position) {
		shader.Use();
		// Vector that decides which direction this object points
		glm::vec3 look = glm::normalize(camera_position - Position);
		// Get right directional vector
		glm::vec3 right = WORLD_UP * look;
		// Get final up vector
		glm::vec3 up = look * right;
		// Build transformation matrix
		glm::mat4 viewTransform{
			right.x, up.x, look.x, Position.x,
			right.y, up.y, look.y, Position.y,
			right.z, up.z, look.z, Position.z,
			0.0f,	 0.0f, 0.0f,   1.0f,
		};
		GLuint viewTLoc = shader.GetUniformLocation("viewTransform");
		glUniformMatrix4fv(viewTLoc, 1, GL_FALSE, glm::value_ptr(viewTransform));
		GLuint viewLoc = shader.GetUniformLocation("view");
		glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));
		glBindVertexArray(VAO);
		glDrawArrays(GL_TRIANGLES, 0, 6);
		glBindVertexArray(0);
	}

	glm::vec3 Scale, Position, Angle;
private:
	GLuint VAO, VBO;
	ShaderProgram& shader;
	glm::mat4 model, normTransform;
};
#endif // !BILLBOARD_H
