#pragma once
#ifndef VIEW_FRUSTUM_H
#define VIEW_FRUSTUM_H
#include "stdafx.h"
// Given position + projection matrix sets up 6 planes representing the view frustum.
// Useful for visibility-based culling.
namespace vulpes {
	namespace util {

		struct view_frustum {
			std::array<glm::vec4, 6> planes;
			glm::vec4& operator[](const size_t& idx) { return planes[idx]; }
		};

	}

}

#endif // !VIEW_FRUSTUM_H
