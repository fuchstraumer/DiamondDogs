#pragma once
#ifndef ICOSPHERE_H
#define ICOSPHERE_H
#include "../stdafx.h"
#include "Mesh.h"
#include "../util/Shader.h"
#include "glm\gtx\simd_mat4.hpp"

using uint = unsigned int;

class IcoSphere : public Mesh {
public:
	IcoSphere(uint lod, float radius);
	IcoSphere() = default;
	~IcoSphere() = default;

	void BuildRenderData();

	void Render(ShaderProgram& shader);

	float Radius;
	uint Max_LOD;
	glm::mat4 Model;
	glm::mat4 NormTransform;
	GLuint VAO, VBO, EBO;
};

#endif // !SPHERE_MESH_H
