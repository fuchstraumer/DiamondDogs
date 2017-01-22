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
	-0.5f, -0.5f, 0.0f,
	0.5f, -0.5f, 0.0f,
	-0.5f,  0.5f, 0.0f,
	0.5f,  0.5f, 0.0f,
};



class Billboard3D {
public:
	// The exact usage of this billboard depends on which shader program is supplied
	// with the constructor.
	Billboard3D() = default;
	~Billboard3D() = default;

	void BuildRenderData(const float& temperature) {
		// Set frame counter to zero
		frame = 0;
		// Bind shader
		Shader->Use();
		// Setup up VAO
		glGenVertexArrays(1, &this->VAO);
		glBindVertexArray(this->VAO);
		// Gen and bind main buffer object
		glGenBuffers(1, &this->VBO);
		glBindBuffer(GL_ARRAY_BUFFER, this->VBO);
		center[0] = Position.x;
		center[1] = Position.y;
		center[2] = Position.z;
		// Populate buffer with data
		glBufferData(GL_ARRAY_BUFFER, sizeof(quad_vertices), &quad_vertices, GL_STATIC_DRAW);
		// Set elements/attributes of the VAO
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(GLfloat), (GLvoid*)0);
		glEnableVertexAttribArray(0);

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
		GLuint modelLoc = Shader->GetUniformLocation("model");
		glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));
		// Pass temperature value
		GLuint tempLoc = Shader->GetUniformLocation("temperature");
		glUniform1i(tempLoc, temperature);
		// Unbind vertex array
		glBindVertexArray(0);
	}

	void Render(const glm::mat4& view, const glm::mat4& projection) {
		Shader->Use();
		glBindVertexArray(this->VAO);
		// Set size of billboard
		GLuint sizeLoc = Shader->GetUniformLocation("size");
		glUniform2f(sizeLoc, Radius, Radius);
		// Pass in the matrices we need
		GLuint viewLoc = Shader->GetUniformLocation("view");
		glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));
		GLuint projLoc = Shader->GetUniformLocation("projection");
		glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(projection));
		// Extract vectors we need from the view matrix
		glm::vec3 camera_right = glm::vec3(view[0][0], view[1][0], view[2][0]);
		glm::vec3 camera_up = glm::vec3(view[0][1], view[1][1], view[2][1]);
		GLuint cRLoc = Shader->GetUniformLocation("cameraRight");
		GLuint cULoc = Shader->GetUniformLocation("cameraUp");
		glUniform3f(cRLoc, camera_right.x, camera_right.y, camera_right.z);
		glUniform3f(cULoc, camera_up.x, camera_up.y, camera_up.z);
		// Set last few uniforms
		GLuint centerLoc = Shader->GetUniformLocation("center");
		glUniform3f(centerLoc, center.x, center.y, center.z);
		// If frame counter is equal to limits of numeric precision,
		if (frame < std::numeric_limits<uint64_t>::max()) {
			// Reset frame counter
			frame = 0;
		}
		// Otherwise, increment it
		else {
			frame++;
		}
		// Set frame value in shader
		GLuint frameLoc = Shader->GetUniformLocation("frame");
		glUniform1i(frameLoc, frame);
		// Draw the billboard
		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	}

	// Sets size of billboard object
	float Radius;
	// Used to set correct model matrix
	glm::vec3 Scale, Position, Angle;
	// Pointer to the shader object: required to avoid issues with 
	// building a Shader program before GL init due to default constructors
	std::shared_ptr<ShaderProgram> Shader;
	
	GLuint starColor;

private:

	// Point the billboard will be centered on
	glm::vec3 center;

	// OpenGL buffer objects
	GLuint VAO, VBO;

	// Private matrices built from input data
	glm::mat4 model, normTransform;

	// Used as a frame counter and passed to shader to evolve noise over time.
	uint64_t frame;
};
#endif // !BILLBOARD_H
