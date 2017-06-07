#pragma once
#ifndef VULPES_PLANAR_MESH_H
#define VULPES_PLANAR_MESH_H

#include "stdafx.h"
#include "mesh.h"
namespace vulpes {

	class PlanarMesh : public Mesh {
	public:

		PlanarMesh(const double& side_length, const size_t& subdivision_level, const glm::vec3& pos = glm::vec3(0.0f), const glm::vec3& scale = glm::vec3(1.0f), const glm::vec3& angle = glm::vec3(0.0f));

		PlanarMesh& operator=(PlanarMesh&& other);

		PlanarMesh() = default;
		virtual ~PlanarMesh() = default;

		size_t SubdivisionLevel;
		double SideLength;

		void Generate();

	};


}

#endif // !VULPES_PLANAR_MESH_H
