#pragma once
#ifndef BILLBOARD_H
#define BILLBOARD_H
#include "stdafx.h"
#include "engine\renderer\objects\device_object.h"
#include "engine\renderer\objects\pipeline_object.h"
#include "engine/renderer/types/vertex_types.h"
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

	Billboard3D(const float& radius, const glm::vec3& pos = glm::vec3(0.0f), const glm::vec3& scale = glm::vec3(1.0f), const glm::vec3& angle = glm::vec3(0.0f));

	Billboard3D(Billboard3D&& other) noexcept;

	Billboard3D & operator=(Billboard3D && other) noexcept;

	void BuildRenderData();

	void Render(const glm::mat4& view);

	void UpdateModelMatrix(glm::vec3 position, glm::vec3 scale, glm::vec3 angle);

	// Sets size of billboard object
	float Radius;
	// Used to set correct model matrix
	glm::vec3 Scale, Position, Angle;
	// Pointer to the shader object: required to avoid issues with 
	// building a Shader program before GL init due to default constructors
	vulpes::program_pipeline_object Program;
	glm::mat4 Projection;
	GLuint starColor;

private:

	// Point the billboard will be centered on
	glm::vec3 center;

	// Device objects
	vulpes::device_object<vulpes::named_buffer_t> VBO;
	vulpes::device_object<vulpes::vertex_array_t> VAO;

	// Private matrices built from input data
	glm::mat4 model, normTransform;
};
#endif // !BILLBOARD_H