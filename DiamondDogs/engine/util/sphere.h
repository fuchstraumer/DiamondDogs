#pragma once
#ifndef VULPES_SPHERE_H
#define VULPES_SPHERE_H

#include "stdafx.h"
#include "view_frustum.h"

namespace vulpes {

	namespace util {

		struct Sphere {
			glm::vec3 Origin;
			double Radius;

			Sphere() = default;
			Sphere(const glm::dvec3& origin, const double& radius) : Origin(origin), Radius(radius) {};
			
			bool ConincidesWith(const Sphere& other) const {
				// distance between spheres, sphere's intersect if this is less than sum of radii
				double dist = glm::length(Origin - other.Origin);
				return dist < Radius + other.Radius;
			}

			bool CoincidesWithFrustum(const view_frustum& frustum) {
				for (const auto& plane : frustum.planes) {
					double dist = glm::dot(glm::vec3(plane.xyz), Origin) + plane.w;
					if (dist < -1.0 * Radius) {
						return false;
					}

					if (std::abs(dist) < Radius) {
						return true;
					}
				}

				return true;
			}

		};

	}

}

#endif // !VULPES_SPHERE_H
