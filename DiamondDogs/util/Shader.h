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
	Shader() = default;

	Shader(const char* file, ShaderType type) {
		Type = type;
		filename = file;
		std::ifstream code_stream;
		std::string tmp; // tmp string for holding the code
		code_stream.exceptions(std::ifstream::badbit);
		try {
			// Attempt to open the given file
			code_stream.open(filename);
			// Create a string stream object
			std::stringstream str;
			// Write the file into the stream
			str << code_stream.rdbuf();
			// And write the stream into this class' code object
			tmp = str.str();
			// Close the file, we're done now
			code_stream.close();
		}
		catch (std::ifstream::failure e){
			std::cerr << "ERROR::SHADER::FILE_NOT_SUCCESFULLY_READ" << std::endl;
		}
		// Reformat into null-terminated C-like string for GLSL compiler
		const GLchar* code = tmp.c_str();

		// Try to compile the shader.
		Handle = glCreateShader(Type);
		glShaderSource(Handle, 1, &code, NULL);
		glCompileShader(Handle);
		// Print compile errors if any
		GLint success;
		GLchar infoLog[1024];
		glGetShaderiv(Handle, GL_COMPILE_STATUS, &success);
		if (!success)
		{
			glGetShaderInfoLog(Handle, 1024, NULL, infoLog);
			std::cout << "ERROR::" << GetTypeName(Type) << "::COMPILATION_FAILED\n" << infoLog << std::endl;
			throw(std::runtime_error("Shader compiliation failed"));
		}
	}

	~Shader() {
		glDeleteShader(Handle);
	}
	
	GLuint Handle;
	ShaderType Type;
	const char* filename;
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

	// Init program
	void Init() {
		Handle = glCreateProgram();
	}
	// Feed in handles to other shaders 
	void AttachShader(const Shader& shader) {
		glAttachShader(Handle, shader.Handle);
	}
	// Link and compile this program
	void CompleteProgram(void){
		glLinkProgram(Handle);
		GLint success;
		GLchar infoLog[1024];
		// Print linking errors if any
		glGetProgramiv(Handle, GL_LINK_STATUS, &success);
		if (!success)
		{
			glGetProgramInfoLog(Handle, 1024, NULL, infoLog);
			std::cout << "ERROR::SHADER::PROGRAM::LINKING_FAILED\n" << infoLog << std::endl;
			throw(std::runtime_error("Shader program linking failed"));
		}
	}

	void Use(void) {
		glUseProgram(Handle);
	}
	GLuint Handle;

	void BuildUniformMap(const std::vector<std::string>& uniforms) {
		for (auto str : uniforms) {
			GLuint loc = glGetUniformLocation(Handle, str.c_str());
			Uniforms.insert(MapEntry(str, loc));
		}
	}

	GLuint GetUniformLocation(const std::string& uniform) {
		return Uniforms.at(uniform);
	}

	void BuildUniformBlock(const GLchar* block_name, const std::vector<GLchar*>& uniform_names) {

		// Prepare index vector for when we get uniform locations.
		std::vector<GLuint> indices;
		indices.resize(uniform_names.size());

		// Get location of the uniform block
		GLuint blockLoc = glGetUniformBlockIndex(Handle, block_name);

		// Get size of region we need to allocate for
		GLint blockSize;
		glGetActiveUniformBlockiv(Handle, blockLoc, GL_UNIFORM_BLOCK_DATA_SIZE, &blockSize);

		// Allocate for the region
		GLubyte *buffer;
		buffer = (GLubyte*)malloc(blockSize);

		// Now, query for the indices of each variable in this uniform block
		glGetUniformIndices(Handle, (GLsizei)uniform_names.size(), uniform_names.data(), indices.data());

		for (GLuint i = 0; i < uniform_names.size(); ++i) {
			std::pair<std::string, GLuint> entry(uniform_names[i], indices[i]);
			Uniforms.insert(entry);
		}
	}

	UniformMap Uniforms;
	
};

// Key is an alias used to name the program, value is the GLuint corresponding
// to the program
using ProgramMap = std::unordered_map<std::string, GLuint>;

class ProgramManager {
public:
	// Stores several shader programs and manages them.
	ProgramManager() = default;
	~ProgramManager() {
		for (auto prgm : Map) {
			
		}
	}

	ProgramMap Map;
};
#endif // !SHADER_H
