#pragma once
#ifndef VULPES_SHADER_OBJECT_H
#define VULPES_SHADER_OBJECT_H

/*

	NOTE: Shader objects are not the same as program pipelines.
	Shader objects in this implementation are seperable shader
	objects that are then plugged into program pipeline objects.

*/

#include "stdafx.h"
#include "device_object.h"

namespace vulpes {

	using vertex_shader_t = std::integral_constant<unsigned int, 1>;
	using fragment_shader_t = std::integral_constant<unsigned int, 2>;
	using geometry_shader_t = std::integral_constant<unsigned int, 3>;
	using tess_control_shader_t = std::integral_constant<unsigned int, 4>;
	using tess_eval_shader_t = std::integral_constant<unsigned int, 5>;
	using compute_shader_t = std::integral_constant<unsigned int, 6>;

	template<typename shader_type, unsigned int version = 450>
	struct shader_object {

		shader_object(const char* filename) {

			std::ifstream shader_stream;
			std::string shader_code;

			// Open shader code file
			try {
				shader_stream.open(filename);
				std::stringstream tmp;
				tmp << shader_stream.rdbuf();
				shader_code = tmp.str();
				shader_stream.close();
			}
			catch (std::ifstream::failure e) {
				std::cerr << "Shader file not found, quitting" << std::endl;
			}

			// Check found file for #include's
			std::vector<std::string> includes;
			find_includes(shader_code, includes);

			// Setup final code. #version MUST (!!!) always be first.
			std::vector<std::string> final_code;
			final_code.push_back(version_string);
			std::copy(includes.cbegin(), includes.cend(), std::back_inserter(final_code));
			final_code.push_back(shader_code);

			// Compile/create
			generate_object_impl(*this, final_code, typename shader_type::type());

		
	
		}

		~shader_object() {
			destroy_object_impl(*this, typename shader_type::type());
		}

		GLuint ID;
		unsigned int version;
	};


	template<typename shader_type, unsigned int version>
	void generate_object_impl(shader_object<shader_type, version>& shader, const std::vector<std::string>& final_code, vertex_shader_t) {
		shader.ID = glCreateShader(GL_VERTEX_SHADER);
	}

	template<typename shader_type, unsigned int version>
	void destroy_object_impl(shader_object<shader_type, version>& shader, vertex_shader_t) {
		glDeleteShader(shader.ID);
	}

	template<typename shader_type, unsigned int version>
	void destroy_object_impl(shader_object<shader_type, version>& shader, fragment_shader_t) {
		glDeleteShader(shader.ID);
	}

	template<typename shader_type, unsigned int version>
	void destroy_object_impl(shader_object<shader_type, version>& shader, geometry_shader_t) {
		glDeleteShader(shader.ID);
	}

	template<typename shader_type, unsigned int version>
	void destroy_object_impl(shader_object<shader_type, version>& shader, tess_control_shader_t) {
		glDeleteShader(shader.ID);
	}

	template<typename shader_type, unsigned int version>
	void destroy_object_impl(shader_object<shader_type, version>& shader, tess_eval_shader_t) {
		glDeleteShader(shader.ID);
	}

	template<typename shader_type, unsigned int version>
	void destroy_object_impl(shader_object<shader_type, version>& shader, compute_shader_t) {
		glDeleteShader(shader.ID);
	}

}

#endif // !VULPES_SHADER_OBJECT_H
