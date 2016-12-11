#pragma once
#ifndef QUAD_H
#define QUAD_H
#include "../stdafx.h"
#include "../util/Shader.h"
/*
	
	QUAD_H

	Used as a simple "screen" upon which to render when using FBO's

*/

static const GLfloat quad_vertices[] = {
	-1.0, -1.0, 0.0, 0.0, 0.0,
	1.0, -1.0, 0.0, 1.0, 0.0,
	1.0,  1.0, 0.0, 1.0, 1.0,
	1.0,  1.0, 0.0, 1.0, 1.0,
	-1.0,  1.0, 0.0, 0.0, 1.0,
	-1.0, -1.0, 0.0, 0.0, 0.0,
};

class Quad {
	Quad() {
		glGenVertexArrays(1, &VAO);
		glBindVertexArray(VAO);
		glGenBuffers(1, &VBO);
		glBindBuffer(GL_ARRAY_BUFFER, VBO);
		glBufferData(GL_ARRAY_BUFFER, sizeof(quad_vertices), quad_vertices, GL_STATIC_DRAW);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), (GLvoid*)0);
		glEnableVertexAttribArray(1);
		glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), (GLvoid*)(3 * sizeof(GLfloat)));
		glBindVertexArray(0);
	}

	void Render(ShaderProgram& shader) {

	}

	GLuint VAO, VBO;
};
#endif // !QUAD_H
