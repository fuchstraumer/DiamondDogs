#pragma once
#ifndef VULPES_UTIL_AABB_H
#define VULPES_UTIL_AABB_H

#include "stdafx.h"

namespace vulpes {

	namespace util {

		struct AABB {

			glm::vec3 Min, Max;
			glm::vec3 Center;
			glm::vec3 Extents;

		};

	}

}

#endif // !VULPES_UTIL_AABB_H
