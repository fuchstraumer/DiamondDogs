#pragma once
#ifndef VULPES_SPHERE_H
#define VULPES_SPHERE_H

#include "stdafx.h"
#include "view_frustum.h"
#include "AABB.h"
namespace vulpes {

	namespace util {

		struct Sphere {
			glm::dvec3 Origin;
			double Radius;

			Sphere() = default;
			Sphere(const glm::dvec3& origin, const double& radius) : Origin(origin), Radius(radius) {};
			
			bool ConincidesWith(const Sphere& other) const {
				// distance between spheres, sphere's intersect if this is less than sum of radii
				double dist = glm::length(Origin - other.Origin);
				return dist < Radius + other.Radius;
			}

			bool CoincidesWithFrustum(const view_frustum& frustum) const {
				for (const auto& plane : frustum.planes) {
					double dist = glm::dot(glm::dvec3(static_cast<double>(plane.x), static_cast<double>(plane.y), static_cast<double>(plane.z)), Origin) + static_cast<double>(plane.w);
					if (dist < -1.0 * Radius) {
						return false;
					}

					if (std::abs(dist) < Radius) {
						return true;
					}
				}

				return true;
			}

			bool CoincidesWith(const AABB& bounds) const {
				// Get distance along each axis, sum it
				double dmin = 0.0;

				if (Origin.x < bounds.Min.x) {
					dmin += std::pow(Origin.x - bounds.Min.x, 2.0);
				}
				else if (Origin.x > bounds.Max.x) {
					dmin += std::pow(Origin.x - bounds.Max.x, 2.0);
				}

				if (Origin.y < bounds.Min.y) {
					dmin += std::pow(Origin.y - bounds.Min.y, 2.0);
				}
				else if (Origin.y > bounds.Max.y) {
					dmin += std::pow(Origin.y - bounds.Max.y, 2.0);
				}

				if (Origin.z < bounds.Min.z) {
					dmin += std::pow(Origin.z - bounds.Min.z, 2.0);
				}
				else if (Origin.z > bounds.Max.z) {
					dmin += std::pow(Origin.z - bounds.Max.z, 2.0);
				}

				return (dmin <= std::pow(Radius, 2.0));
			}

		};

	}

}

#endif // !VULPES_SPHERE_H
