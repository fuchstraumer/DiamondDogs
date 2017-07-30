#pragma once
#ifndef VULPES_UTIL_AABB_H
#define VULPES_UTIL_AABB_H

#include "stdafx.h"
#include "engine\objects\mesh\Mesh.h"
#include "view_frustum.h"
#include "VulpesRender/include/render\GraphicsPipeline.h"
#include "VulpesRender/include/resource\ShaderModule.h"
#include "VulpesRender/include/resource\Buffer.h"

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
