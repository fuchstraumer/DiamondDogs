#pragma once
#ifndef VULPES_UTIL_AABB_H
#define VULPES_UTIL_AABB_H

#include "stdafx.h"
#include "view_frustum.h"
#include "engine\renderer\render\GraphicsPipeline.h"
#include "engine\renderer\resource\ShaderModule.h"

namespace vulpes {

	namespace util {

		struct AABB {

			glm::dvec3 Min, Max;
			
			glm::dvec3 Extents() const {
				return Min - Max * 0.5;
			}

			glm::dvec3 Center() const {
				return (Min + Max) / 2.0;
			}

			bool InFrustum(const view_frustum& view) {

			}

		};

	}

}

#endif // !VULPES_UTIL_AABB_H
