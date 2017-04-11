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

		program_pipeline_object();

		~program_pipeline_object();

		program_pipeline_object(program_pipeline_object&& other) noexcept;

		program_pipeline_object& operator=(program_pipeline_object&& other) noexcept;

		void attach_program(const GLuint& _program_id, GLbitfield stages);

		void use();

		const GLuint at(const std::string& uniform_name) const;

		void setup_uniforms();

		GLuint program_id;

		std::unordered_map<std::string, GLint> uniforms;
	};

}

#endif // !VULPES_PIPELINE_OBJECT_H
