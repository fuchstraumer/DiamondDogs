#pragma once
#ifndef BILLBOARD_H
#define BILLBOARD_H
#include "stdafx.h"
#include "../util/Shader.h"

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

	void BuildRenderData();

	void Render(const glm::mat4& view, const glm::mat4& projection);

	void UpdateModelMatrix(glm::vec3 position, glm::vec3 scale, glm::vec3 angle);

	// Sets size of billboard object
	float Radius;
	// Used to set correct model matrix
	glm::vec3 Scale, Position, Angle;
	// Pointer to the shader object: required to avoid issues with 
	// building a Shader program before GL init due to default constructors
	std::shared_ptr<ShaderProgram> Program;
	
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
