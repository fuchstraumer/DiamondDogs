#pragma once
#ifndef VULPES_UTIL_AABB_H
#define VULPES_UTIL_AABB_H

#include "stdafx.h"
#include "view_frustum.h"

namespace vulpes {

	namespace util {

		struct AABB {

			glm::dvec3 Min, Max;
			
			glm::dvec3 Extents() const {
				return Min - Max * 0.5;
			}

			bool IntersectsTriangle(const glm::vec3& ray_origin, const glm::vec3& ray_direction, const std::array<glm::vec3, 3>& vertices);

			float TriangleDistance(const glm::vec3& ray_origin, const glm::vec3& ray_direction, const std::array<glm::vec3, 3>& vertices);

			glm::dvec3 Center() const {
				return (Min + Max) / 2.0;
			}

		};

	}

}

#endif // !VULPES_UTIL_AABB_H
