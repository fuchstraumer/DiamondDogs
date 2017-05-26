#pragma once
#ifndef VULPES_UTIL_AABB_H
#define VULPES_UTIL_AABB_H

#include "stdafx.h"
#include "view_frustum.h"

namespace vulpes {

	namespace util {

		struct AABB {

			glm::vec3 Min, Max;
			glm::vec3 Center;
			glm::vec3 Extents;

			bool IntersectsTriangle(const glm::vec3& ray_origin, const glm::vec3& ray_direction, const std::array<glm::vec3, 3>& vertices);

			float TriangleDistance(const glm::vec3& ray_origin, const glm::vec3& ray_direction, const std::array<glm::vec3, 3>& vertices);

		};

	}

}

#endif // !VULPES_UTIL_AABB_H
