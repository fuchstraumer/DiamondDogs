#pragma once
#ifndef VULPES_VERTEX_TYPES_H
#define VULPES_VERTEX_TYPES_H
#include "stdafx.h"
#include <boost\fusion\adapted.hpp>
#include <boost\fusion\adapted\struct.hpp>
namespace vulpes {

	struct minimal_vertex_t {
		glm::vec3 position;
	};

	struct base_vertex_t {
		glm::vec3 position;
		glm::vec3 normal;
	};

	struct textured_vertex_t {
		glm::vec3 position;
		glm::vec3 normal;
		glm::vec2 uv;
	};

	struct detail_vertex_t {
		glm::vec3 position_high, position_low, normal;
		glm::vec2 uv;
	};
}

BOOST_FUSION_ADAPT_STRUCT
(
	vulpes::minimal_vertex_t,
	(glm::vec3, position)
)

BOOST_FUSION_ADAPT_STRUCT
(
	vulpes::base_vertex_t,
	(glm::vec3, position)
	(glm::vec3, normal)
)

BOOST_FUSION_ADAPT_STRUCT
(
	vulpes::textured_vertex_t,
	(glm::vec3, position)
	(glm::vec3, normal)
	(glm::vec2, uv)
)


#endif // !VULPES_VERTEX_TYPES_H
