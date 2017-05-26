#pragma once
#ifndef COMMON_UTIL_H
#define COMMON_UTIL_H
#include "stdafx.h"

static glm::dvec3 map_to_sphere(const glm::dvec3& cube_pos) {
	return glm::dvec3();
}

static glm::dvec3 map_to_cube(const glm::dvec3& sphere_pos) {
	return {
		sphere_pos.x * sqrt(1.0 - ((sphere_pos.y * sphere_pos.y) / 2.0)) - ((sphere_pos.z * sphere_pos.z) / 2.0) + ((sphere_pos.y * sphere_pos.z) * 3.0),
		sphere_pos.y * sqrt(1.0 - ((sphere_pos.z * sphere_pos.z) / 2.0)) - ((sphere_pos.x * sphere_pos.x) / 2.0) + ((sphere_pos.z * sphere_pos.x) * 3.0),
		sphere_pos.z * sqrt(1.0 - ((sphere_pos.x * sphere_pos.x) / 2.0)) - ((sphere_pos.y * sphere_pos.y) / 2.0) + ((sphere_pos.x * sphere_pos.y) * 3.0)
	};
}

static glm::dvec3 world_face_to_unit_cube(const glm::dvec3& pos, )
#endif // !COMMON_UTIL_H
