#include "stdafx.h"
#include "PlanarMesh.h"

namespace vulpes {
	namespace mesh {

		PlanarMesh::PlanarMesh(const double& side_length, const size_t& subdivision_level, const glm::vec3& pos, const glm::vec3& scale, const glm::vec3& angle) : Mesh(pos, scale, angle), SubdivisionLevel(subdivision_level), SideLength(side_length) {}

		PlanarMesh& PlanarMesh::operator=(PlanarMesh && other){
			SideLength = std::move(other.SideLength);
			SubdivisionLevel = std::move(other.SubdivisionLevel);
			position = std::move(other.position);
			scale = std::move(other.scale);
			angle = std::move(other.angle);
			vbo = std::move(other.vbo);
			ebo = std::move(other.ebo);
			device = std::move(other.device);
			model = std::move(other.model);
			indices = std::move(other.indices);
			vertices = std::move(other.vertices);
			return *this;
		}

		void PlanarMesh::Generate() {
			double step = SideLength / static_cast<double>(SubdivisionLevel);
			vertices.positions.reserve(SubdivisionLevel*SubdivisionLevel);
			vertices.normals_uvs.reserve(SubdivisionLevel*SubdivisionLevel);
			for (size_t i = 0; i <= SubdivisionLevel; ++i) {
				for (size_t j = 0; j <= SubdivisionLevel; ++j) {
					add_vertex({ glm::vec3(static_cast<float>(i) * static_cast<float>(step), 0.0f, static_cast<float>(j) * static_cast<float>(step)) });
				}
			}
			indices.reserve(SubdivisionLevel*SubdivisionLevel);
			for (size_t i = 0; i < SubdivisionLevel; ++i) {
				for (size_t j = 0; j < SubdivisionLevel; ++j) {
					index_t i0 = i + (j * SubdivisionLevel + 1);
					index_t i1 = i0 + 1;
					index_t i2 = i0 + SubdivisionLevel + 1;
					index_t i3 = i2 + 1;
					add_triangle(i0, i1, i2);
					add_triangle(i2, i1, i3);
				}
			}
			indices.shrink_to_fit();
		}
	}
}
