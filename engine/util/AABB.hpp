#pragma once
#ifndef VULPES_UTIL_AABB_H
#define VULPES_UTIL_AABB_H

#include "stdafx.h"
#include "engine\objects\mesh\Mesh.hpp"
#include "view_frustum.hpp"
#include "VulpesRender/include/render\GraphicsPipeline.hpp"
#include "VulpesRender/include/resource\ShaderModule.hpp"
#include "VulpesRender/include/resource\Buffer.hpp"

namespace vulpes {

	namespace util {

		struct AABB {

			glm::vec3 Min, Max;
			
			glm::vec3 Extents() const;

			glm::vec3 Center() const;

			void UpdateMinMax(const float& y_min, const float& y_max);

		};

		

	}

}

#endif // !VULPES_UTIL_AABB_H
