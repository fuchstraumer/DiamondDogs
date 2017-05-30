#pragma once
#ifndef VULPES_UTIL_AABB_H
#define VULPES_UTIL_AABB_H

#include "stdafx.h"
#include "engine\objects\Mesh.h"
#include "view_frustum.h"
#include "engine\renderer\render\GraphicsPipeline.h"
#include "engine\renderer\resource\ShaderModule.h"


namespace vulpes {

	namespace util {

		struct AABB {

			glm::dvec3 Min, Max;
			
			glm::dvec3 Extents() const;

			glm::dvec3 Center() const;


		};

	}

}

#endif // !VULPES_UTIL_AABB_H
