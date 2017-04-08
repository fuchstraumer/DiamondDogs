#pragma once
#ifndef VULPES_DEVICE_OBJECT_H
#define VULPES_DEVICE_OBJECT_H
#include "stdafx.h"

namespace vulpes {
	
	using vertex_array_t = std::integral_constant<unsigned int, 0>;
	using named_buffer_t = std::integral_constant<unsigned int, 1>;
	using named_framebuffer_t = std::integral_constant<unsigned int, 2>;
	using program_pipeline_t = std::integral_constant<unsigned int, 4>;
	using texture_1d_t = std::integral_constant<unsigned int, 6>;
	using texture_2d_t = std::integral_constant<unsigned int, 7>;
	using texture_3d_t = std::integral_constant<unsigned int, 8>;

	template<typename object_type, size_t num_objects = 1>
	struct device_object {

		device_object() {
			generate_object_impl(*this, typename object_type::type());
			//std::cerr << "Created a resource" << std::endl;
		}

		~device_object() {
			// objects that have had data moved out of them will still reach this dtor, only call 
			// GL object destroy func if elements of handles are not zero
			if (!std::any_of(handles.cbegin(), handles.cend(), [](const GLuint& n) { return n == 0; })) {
				destroy_object_impl(*this, typename object_type::type());
			}
			//std::cerr << "Destroyed a resource" << std::endl;
		}

		void generate_object() {
			generate_object_impl(*this, typename object_type::type());
		}

		// address/name of the resource in the OpenGL API
		std::array<GLuint, num_objects> handles;

		// Operator to access individual object handles
		const GLuint& operator[](const size_t& idx) const {
			return handles[idx];
		}

		// Delete copy ctor+operator, can't copy a device/OpenGL object
		device_object(const device_object& other) = delete;
		device_object& operator=(const device_object& other) = delete;

		// Move ctor and operator okay, just have to zero other array.
		device_object(device_object&& other) noexcept : handles(other.handles) {
			other.handles.assign(0);
		}

		device_object& operator=(device_object&& other) noexcept {
			handles = other.handles;
			other.handles.assign(0);
			return *this;
		}

	};

	/*
		Build OpenGL DSA objects
	*/

	template<typename T, size_t cnt>
	void generate_object_impl(device_object<T, cnt>& object, vertex_array_t) {
		glCreateVertexArrays(cnt, &object.handles[0]);
	}

	template<typename T, size_t cnt>
	void generate_object_impl(device_object<T, cnt>& object, named_buffer_t) {
		glCreateBuffers(cnt, &object.handles[0]);
	}

	template<typename T, size_t cnt>
	void generate_object_impl(device_object<T, cnt>& object, texture_1d_t) {
		glCreateTextures(GL_TEXTURE_1D, cnt, &object.handles[0]);
	}

	template<typename T, size_t cnt>
	void generate_object_impl(device_object<T, cnt>& object, texture_2d_t) {
		glCreateTextures(GL_TEXTURE_2D, cnt, &object.handles[0]);
	}

	template<typename T, size_t cnt>
	void generate_object_impl(device_object<T, cnt>& object, texture_3d_t) {
		glCreateTextures(GL_TEXTURE_3D, cnt, &object.handles[0]);
	}

	template<typename T, size_t cnt>
	void generate_object_impl(device_object<T, cnt>& object, named_framebuffer_t) {
		glCreateFramebuffers(cnt, &object.handles[0]);
	}

	template<typename T, size_t cnt>
	void generate_object_impl(device_object<T, cnt>& object, program_pipeline_t) {
		glCreateProgramPipelines(cnt, &object.handles[0]);
	}

	/*
		Destroy/Delete OpenGL objects
	*/

	template<typename T, size_t cnt>
	void destroy_object_impl(device_object<T, cnt>& object, vertex_array_t) {
		glDeleteVertexArrays(cnt, &object.handles[0]);
	}

	template<typename T, size_t cnt>
	void destroy_object_impl(device_object<T, cnt>& object, named_buffer_t) {
		glDeleteBuffers(cnt, &object.handles[0]);
	}

	template<typename T, size_t cnt>
	void destroy_object_impl(device_object<T, cnt>& object, texture_1d_t) {
		glDeleteTextures(cnt, &object.handles[0]);
	}

	template<typename T, size_t cnt>
	void destroy_object_impl(device_object<T, cnt>& object, texture_2d_t) {
		glDeleteTextures(cnt, &object.handles[0]);
	}

	template<typename T, size_t cnt>
	void destroy_object_impl(device_object<T, cnt>& object, texture_3d_t) {
		glDeleteTextures(cnt, &object.handles[0]);
	}

	template<typename T, size_t cnt>
	void destroy_object_impl(device_object<T, cnt>& object, named_framebuffer_t) {
		glDeleteFramebuffers(cnt, &object.handles[0]);
	}

	template<typename T, size_t cnt>
	void destroy_object_impl(device_object<T, cnt>& object, program_pipeline_t) {
		glDeleteProgramPipelines(cnt, &object.handles[0]);
	}
	
}
#endif //!VULPES_DEVICE_object_H