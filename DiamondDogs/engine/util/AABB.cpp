#include "stdafx.h"
#include "AABB.h"

glm::dvec3 vulpes::util::AABB::Extents() const {
	return Min - Max * 0.5;
}

glm::dvec3 vulpes::util::AABB::Center() const {
	return (Min + Max) / 2.0;
}
