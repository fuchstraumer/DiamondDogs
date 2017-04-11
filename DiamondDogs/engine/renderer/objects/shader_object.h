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

}

#endif // !VULPES_SHADER_OBJECT_H
