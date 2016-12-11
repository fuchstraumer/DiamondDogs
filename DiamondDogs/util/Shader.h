#pragma once
#ifndef SHADER_H

#define SHADER_H

#include "../stdafx.h"
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <string>
/*
	
	SHADER_H

	New and improved shader class, using many ideas from the original shader class
	based off of the LearnOpenGL.com tutorial, but adding new features.

	- Map for keeping track of Uniform locations, meaning uniforms only have to be 
	  mapped once per compiliation

	- Ability to selectively compile and add different shader types, instead of having
	  to supply all of them at once.

	Shader class - used to hold individual shaders of various types

	ShaderProgram class - final object that shaders are combined into
	Contains the map of uniform locations
*/

enum ShaderType : GLenum {
	VERTEX_SHADER = GL_VERTEX_SHADER,
	FRAGMENT_SHADER =  GL_FRAGMENT_SHADER,
	GEOMETRY_SHADER = GL_GEOMETRY_SHADER,
	T_CONTROL_SHADER = GL_TESS_CONTROL_SHADER,
	T_EVAL_SHADER = GL_TESS_EVALUATION_SHADER,
	COMPUTE_SHADER = GL_COMPUTE_SHADER,
};

inline std::string GetTypeName(const ShaderType& t) {
	std::string res;
	switch (t) {
	case VERTEX_SHADER:
		res = std::string("VERTEX_SHADER");
		break;
	case FRAGMENT_SHADER:
		res = std::string("FRAGMENT_SHADER");
		break;
	case GEOMETRY_SHADER:
		res = std::string("GEOMETRY_SHADER");
		break;
	case T_CONTROL_SHADER:
		res = std::string("T_CONTROL_SHADER");
		break;
	case T_EVAL_SHADER:
		res = std::string("T_EVAL_SHADER");
		break;
	case COMPUTE_SHADER:
		res = std::string("COMPUTE_SHADER");
		break;
	}
	return res;
}


class Shader {
public:

	~Shader();

	Shader& operator=(Shader&& other) {
		this->Handle = other.Handle;
		other.Handle = 0;
	}

	Shader(Shader&& other) : Handle(other.Handle) {
		other.Handle = 0;
	}

	Shader& operator=(const Shader& other) & = delete;
	Shader(const Shader&) = delete;
	Shader(const char* file, ShaderType type);
	
	GLuint Handle;
};

using UniformMap = std::unordered_map<std::string, GLuint>;
using MapEntry = std::pair<std::string, GLuint>;

class ShaderProgram {
public:
	ShaderProgram() {
		Handle = 0;
	}

	~ShaderProgram() {
		glDeleteProgram(Handle);
	}

	ShaderProgram& operator=(const ShaderProgram& other) & = delete;

	ShaderProgram& operator=(ShaderProgram&& other) {
		this->Handle = other.Handle;
		other.Handle = 0;
	}

	ShaderProgram(ShaderProgram&& other) : Handle(other.Handle) {
		other.Handle = 0;
	}
	// Init program
	void Init();

	// Feed in handles to other shaders 
	void AttachShader(const Shader& shader);
	// Link and compile this program
	void CompleteProgram(void);

	void Use(void);

	GLuint Handle;

	void BuildUniformMap(const std::vector<std::string>& uniforms);

	GLuint GetUniformLocation(const std::string& uniform);

	UniformMap Uniforms;
	
};

#endif // !SHADER_H
