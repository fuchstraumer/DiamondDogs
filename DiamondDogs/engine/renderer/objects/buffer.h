#pragma once
#ifndef VULPES_BUFFER_H
#define VULPES_BUFFER_H

#include "stdafx.h"
#include "device_object.h"

namespace vulpes {

	using vpos_buffer_t = std::integral_constant<unsigned int, 0>;
	using vnorm_buffer_t = std::integral_constant<unsigned int, 0>;
	using v_uv_buffer_t = std::integral_constant<unsigned int, 0>;
	using index_buffer_t = std::integral_constant<unsigned int, 0>;
	
	template<typename buffer_t, typename vertex_t, size_t cnt = 1>
	struct buffer : device_object<named_buffer_t, cnt> {
		buffer() : device_object() {};

		bind_buffer() {

		}
	};

}
#endif //!VULPES_BUFFER_H