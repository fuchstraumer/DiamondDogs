#pragma once
#ifndef VULPES_UNIFORM_BUFFER_OBJECT_H
#define VULPES_UNIFORM_BUFFER_OBJECT_H

#include "stdafx.h"
#include "device_object.h"
namespace vulpes {

	namespace detail {

		struct global_ubo {
			glm::mat4 view;
			glm::mat4 projection;
		};

		struct atmo_ubo {
			glm::vec3 inv_wavelength;
			float inner_radius;
			float outer_radius;
			float KrESun;
			float KmESun;
			float Kr4PI;
			float Km4PI;
			float scale;
			float scale_depth;
			float scale_over_scale_depth;
			float g;
			int samples;
		};

	}

	constexpr GLuint ubo_globals_location = 0;
	constexpr GLsizei ubo_globals_size = sizeof(detail::global_ubo);
	constexpr GLuint ubo_lighting_location = 1;
	constexpr GLuint ubo_atmosphere_location = 2;
	constexpr GLsizei ubo_atmosphere_size = sizeof(detail::atmo_ubo);

	struct uniform_buffer_object : public device_object<named_buffer_t> {
		static GLint offset;
		static GLint buffer_size;
		GLuint* ptr;
		uniform_buffer_object(const GLsizei& ubo_size) : device_object(), ptr(nullptr) {
			GLbitfield buffer_bits = (GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT);
			// allocate storage
			glNamedBufferStorage(handles[0], ubo_size, nullptr, buffer_bits);
			// get ptr to buffer
			ptr = static_cast<GLuint*>(glMapNamedBufferRange(handles[0], 0, ubo_size, buffer_bits));
		}

		void set_uniform_at(const void* item, const GLsizei& offset_of_item, const GLsizei& size_of_item) {
			glNamedBufferSubData(handles[0], offset_of_item, size_of_item, item);
		}
	};

}
#endif // !VULPES_UNIFORM_BUFFER_OBJECT_H
