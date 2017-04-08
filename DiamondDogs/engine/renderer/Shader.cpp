#include "stdafx.h"
#include "Shader.h"

void ShaderProgram::Init() {
	Handle = glCreateProgram();
}

void ShaderProgram::AttachShader(const Shader & shader) {
	glAttachShader(Handle, shader.Handle);
}

void ShaderProgram::CompleteProgram(void){
	glLinkProgram(Handle);
	GLint success;
	GLchar infoLog[1024];
	// Print linking errors if any
	glGetProgramiv(Handle, GL_LINK_STATUS, &success);
	if (!success){
		glGetProgramInfoLog(Handle, 1024, NULL, infoLog);
		std::cout << "ERROR::SHADER::PROGRAM::LINKING_FAILED\n" << infoLog << std::endl;
		throw(std::runtime_error("Shader program linking failed"));
	}
}

void ShaderProgram::Use(void) {
	glUseProgram(Handle);
}

void ShaderProgram::BuildUniformMap(const std::vector<std::string>& uniforms){
	for (auto str : uniforms) {
		GLuint loc = glGetUniformLocation(Handle, str.c_str());
		Uniforms.insert(MapEntry(str, loc));
	}
}

GLuint ShaderProgram::GetUniformLocation(const std::string & uniform){
	return Uniforms.at(uniform);
}

Shader::~Shader(){
	glDeleteShader(Handle);
}



Shader::Shader(const char * file, ShaderType type) {
	std::ifstream code_stream;
	std::string tmp; // tmp string for holding the code
	code_stream.exceptions(std::ifstream::badbit);
	try {
		// Attempt to open the given file
		code_stream.open(file);
		// Create a string stream object
		std::stringstream str;
		// Write the file into the stream
		str << code_stream.rdbuf();
		// And write the stream into this class' code object
		tmp = str.str();
		// Close the file, we're done now
		code_stream.close();
	}
	catch (std::ifstream::failure e) {
		std::cerr << "ERROR::SHADER::FILE_NOT_SUCCESFULLY_READ" << std::endl;
	}
	// Reformat into null-terminated C-like string for GLSL compiler
	const GLchar* code = tmp.c_str();

	// Try to compile the shader.
	Handle = glCreateShader(type);
	glShaderSource(Handle, 1, &code, NULL);
	glCompileShader(Handle);
	// Print compile errors if any
	GLint success;
	GLchar infoLog[1024];
	glGetShaderiv(Handle, GL_COMPILE_STATUS, &success);
	if (!success)
	{
		glGetShaderInfoLog(Handle, 1024, NULL, infoLog);
		std::cout << "ERROR::" << GetTypeName(type) << "::COMPILATION_FAILED\n" << infoLog << std::endl;
		throw(std::runtime_error("Shader compiliation failed"));
	}
}
