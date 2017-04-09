#pragma once
#ifndef VULPES_PIPELINE_OBJECT_H
#define VULPES_PIPELINE_OBJECT_H

/*

	Note: This is a program pipeline object
	The program pipeline object struct mainly
	only exists to allow for mapping of the uniforms
	at compile time, and to make setting uniforms easier.

	Its otherwise not built up much beyond the implementation
	in device_object.h

*/

#include "stdafx.h"
#include "device_object.h"
#include "shader_object.h"

namespace vulpes {

	struct program_pipeline_object : device_object<program_pipeline_t> {

		program_pipeline_object() : device_object() {}

		void setup_uniforms() {
			GLint uniform_count = 0;
			GLsizei length = 0, size = 0;
			GLenum type = GL_NONE;
			glGetProgramiv(this->handles[0], GL_ACTIVE_UNIFORMS, &uniform_count);
			for (GLint i = 0; i < uniform_count; i++){
				std::array<GLchar, 0xff> uniform_name = {};
				glGetActiveUniform(this->handles[0], i, uniform_name.size(), &length, &size, &type, uniform_name.data());
				uniforms[uniform_name.data()] = glGetUniformLocation(this->handles[0], uniform_name.data());
			}
		}

		std::unordered_map<std::string, GLint> uniforms;
	};

}

#endif // !VULPES_PIPELINE_OBJECT_H
