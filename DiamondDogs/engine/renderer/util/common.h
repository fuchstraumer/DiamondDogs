#pragma once
#ifndef VULPES_UTIL_COMMON_H
#define VULPES_UTIL_COMMON_H

#include "stdafx.h"

namespace vulpes {

	namespace util {

		std::pair<glm::vec3, glm::vec3> dvec3_to_fvec3(const glm::dvec3& vec) {
			auto double_to_floats = [](const double& value)->std::pair<float, float> {
				std::pair<float, float> result;
				if (value >= 0.0) {
					double high = floorf(value / 65536.0) * 65536.0;
					result.first = (float)high;
					result.second = (float)(value - high);
				}
				else {
					double high = floorf(-value / 65536.0) * 65536.0;
					result.first = (float)-high;
					result.second = (float)(value + high);
				}
				return result;
			};
			auto xx = double_to_floats(vec.x);
			auto yy = double_to_floats(vec.y);
			auto zz = double_to_floats(vec.z);
			return std::make_pair(glm::vec3(xx.first, yy.first, zz.first), glm::vec3(xx.second, yy.second, zz.second));
		}

	}
}

#endif // VULPES_UTIL_COMMON_H
