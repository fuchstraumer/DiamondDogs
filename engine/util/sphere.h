#pragma once
#ifndef VULPES_SPHERE_H
#define VULPES_SPHERE_H

#include "stdafx.h"
#include "view_frustum.hpp"
#include "AABB.hpp"
namespace vulpes {

	namespace util {

		struct Sphere {

			glm::vec3 Origin;
			float Radius;


			Sphere() = default;
			Sphere(const glm::vec3& origin, const float& radius) : Origin(origin), Radius(radius) {};
			
			bool ConincidesWith(const Sphere& other) const {
				// distance between spheres, sphere's intersect if this is less than sum of radii
				float dist = glm::length(Origin - other.Origin);
				return dist < Radius + other.Radius;
			}

			bool CoincidesWithFrustum(const view_frustum& frustum) const {
				for (auto i = 0; i < frustum.planes.size(); i++) {
					if ((frustum.planes[i].x * Origin.x) + (frustum.planes[i].y * Origin.y) + (frustum.planes[i].z * Origin.z) + frustum.planes[i].w <= -Radius) {
						return false;
					}
				}
				return true;
			}

			bool CoincidesWith(const AABB& bounds) const {
				// Get distance along each axis, sum it
				float dmin = 0.0;

				if (Origin.x < bounds.Min.x) {
					dmin += std::powf(Origin.x - bounds.Min.x, 2.0);
				}
				else if (Origin.x > bounds.Max.x) {
					dmin += std::powf(Origin.x - bounds.Max.x, 2.0);
				}

				if (Origin.y < bounds.Min.y) {
					dmin += std::powf(Origin.y - bounds.Min.y, 2.0);
				}
				else if (Origin.y > bounds.Max.y) {
					dmin += std::powf(Origin.y - bounds.Max.y, 2.0);
				}

				if (Origin.z < bounds.Min.z) {
					dmin += std::powf(Origin.z - bounds.Min.z, 2.0);
				}
				else if (Origin.z > bounds.Max.z) {
					dmin += std::powf(Origin.z - bounds.Max.z, 2.0);
				}

				return (dmin <= std::pow(Radius, 2.0));
			}

		};

	}

}

#endif // !VULPES_SPHERE_H
