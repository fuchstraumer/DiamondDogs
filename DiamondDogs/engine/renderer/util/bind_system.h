#pragma once
#ifndef VULPES_BIND_SYSTEM_H
#define VULPES_BIND_SYSTEM_H
#include "stdafx.h"

namespace vulpes {
	namespace util {
		/*

		This is objectively bad right now.

		Note the terrible check for the "not_equal_to" operator that I use to distinguish between
		the struct we're iterating through and the glm:: items it contains :/

		*/

		template<typename T>
		static constexpr void bind_attrib_format_impl(const GLuint& vao_name, const GLuint& current_attribute_idx) {}

		template<>
		static constexpr void bind_attrib_format_impl<float>(const GLuint& vao_name, const GLuint& current_attribute_idx) {
			glVertexArrayAttribFormat(vao_name, current_attribute_idx, 1, GL_FLOAT, GL_FALSE, 0);
		}

		template<>
		static constexpr void bind_attrib_format_impl<int>(const GLuint& vao_name, const GLuint& current_attribute_idx) {
			glVertexArrayAttribFormat(vao_name, current_attribute_idx, 1, GL_INT, GL_FALSE, 0);
		}

		template<>
		static constexpr void bind_attrib_format_impl<unsigned int>(const GLuint& vao_name, const GLuint& current_attribute_idx) {
			glVertexArrayAttribFormat(vao_name, current_attribute_idx, 1, GL_UNSIGNED_INT, GL_FALSE, 0);
		}

		template<>
		static constexpr void bind_attrib_format_impl<double>(const GLuint& vao_name, const GLuint& current_attribute_idx) {
			glVertexArrayAttribFormat(vao_name, current_attribute_idx, 1, GL_DOUBLE, GL_FALSE, 0);
		}

		template<>
		static constexpr void bind_attrib_format_impl<glm::vec2>(const GLuint& vao_name, const GLuint& current_attribute_idx) {
			glVertexArrayAttribFormat(vao_name, current_attribute_idx, 2, GL_FLOAT, GL_FALSE, 0);
		}

		template<>
		static constexpr void bind_attrib_format_impl<glm::ivec2>(const GLuint& vao_name, const GLuint& current_attribute_idx) {
			glVertexArrayAttribFormat(vao_name, current_attribute_idx, 2, GL_INT, GL_FALSE, 0);
		}

		template<>
		static constexpr void bind_attrib_format_impl<glm::uvec2>(const GLuint& vao_name, const GLuint& current_attribute_idx) {
			glVertexArrayAttribFormat(vao_name, current_attribute_idx, 2, GL_UNSIGNED_INT, GL_FALSE, 0);
		}

		template<>
		static constexpr void bind_attrib_format_impl<glm::dvec2>(const GLuint& vao_name, const GLuint& current_attribute_idx) {
			glVertexArrayAttribFormat(vao_name, current_attribute_idx, 2, GL_DOUBLE, GL_FALSE, 0);
		}

		template<>
		static constexpr void bind_attrib_format_impl<glm::vec3>(const GLuint& vao_name, const GLuint& current_attribute_idx) {
			glVertexArrayAttribFormat(vao_name, current_attribute_idx, 3, GL_FLOAT, GL_FALSE, 0);
		}

		template<>
		static constexpr void bind_attrib_format_impl<glm::ivec3>(const GLuint& vao_name, const GLuint& current_attribute_idx) {
			glVertexArrayAttribFormat(vao_name, current_attribute_idx, 3, GL_INT, GL_FALSE, 0);
		}

		template<>
		static constexpr void bind_attrib_format_impl<glm::uvec3>(const GLuint& vao_name, const GLuint& current_attribute_idx) {
			glVertexArrayAttribFormat(vao_name, current_attribute_idx, 3, GL_UNSIGNED_INT, GL_FALSE, 0);
		}

		template<>
		static constexpr void bind_attrib_format_impl<glm::dvec3>(const GLuint& vao_name, const GLuint& current_attribute_idx) {
			glVertexArrayAttribFormat(vao_name, current_attribute_idx, 3, GL_DOUBLE, GL_FALSE, 0);
		}

		/*

		Binding structs

		*/

		template<typename T>
		struct binder;

		template<typename S, typename N>
		struct bind_struct_recursive {
			typedef typename boost::fusion::result_of::value_at<S, N>::type current_t;
			typedef typename boost::mpl::next<N>::type next_t;

			static inline void bind(const GLuint& vao_name, const GLuint& vbo_name, GLuint& curr_attribute_idx) {
				binder<current_t>::bind(vao_name, vbo_name, curr_attribute_idx);
				bind_struct_recursive<S, next_t>::bind(vao_name, vbo_name, curr_attribute_idx);
			}
		};

		// Specialized struct defining end of struct, do nothing/bind nothing here
		template<typename S>
		struct bind_struct_recursive<S, typename boost::fusion::result_of::size<S>::type> {
			static inline void bind(const GLuint& vao_name, const GLuint& vbo_name, GLuint& curr_attribute_idx) {}
		};

		template<typename S>
		struct bind_struct_init : bind_struct_recursive<S, typename boost::mpl::int_<0>> {};

		template<typename T>
		struct bind_struct {

			typedef bind_struct<T> type;

			static inline void bind(const GLuint& vao_name, const GLuint& vbo_name, GLuint& curr_attribute_idx) {
				bind_struct_init<T>::bind(vao_name, vbo_name, curr_attribute_idx);
			}
		};

		template<typename T>
		struct bind_type {

			typedef bind_type<T> type;

			static inline void bind(const GLuint& vao_name, const GLuint& vbo_name, GLuint& current_attribute_idx) {

				glEnableVertexArrayAttrib(vao_name, current_attribute_idx);
				glVertexArrayVertexBuffer(vao_name, current_attribute_idx, vbo_name, 0, sizeof(T));
				bind_attrib_format_impl<T>(vao_name, current_attribute_idx);
				current_attribute_idx += 1;
			}
		};

		template<typename T>
		struct choose_binder {
			typedef
				typename boost::mpl::eval_if<boost::has_not_equal_to<T>, boost::mpl::identity<bind_type<T>>,
				typename boost::mpl::eval_if<boost::is_class<T>, boost::mpl::identity<bind_struct<T>>, boost::mpl::identity<bind_type<T>>>>::type type;
		};

		// used to inherit a type
		template<typename T>
		struct binder : public choose_binder<T>::type {};


		template<typename T>
		struct bind_system {
			static void bind_vertex(const GLuint& vao_name, const GLuint& vbo_name) {
				GLuint attr = 0;
				binder<T>::bind(vao_name, vbo_name, attr);
			}
		};

	}
}

#endif // !VULPES_BIND_SYSTEM_H
