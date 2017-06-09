#include "stdafx.h"
#include "PlanarMesh.h"

namespace vulpes {
	namespace mesh {
		PlanarMesh::PlanarMesh(const double& side_length, const glm::ivec3& grid_pos, const glm::vec3& pos, const glm::vec3& scale, const glm::vec3& angle) : Mesh(pos, scale, angle), SideLength(side_length), GridPos(grid_pos) {}

		PlanarMesh& PlanarMesh::operator=(PlanarMesh && other) {
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
			GridPos = std::move(other.GridPos);
			return *this;
		}

		void PlanarMesh::Generate(terrain::HeightNodeLoader* height_nodes) {
			SubdivisionLevel = terrain::HeightNode::RootNodeSize;

			size_t count2 = SubdivisionLevel + 1;
			size_t numTris = SubdivisionLevel*SubdivisionLevel * 6;
			size_t numVerts = count2*count2;
			float scale = static_cast<float>(SideLength) / static_cast<float>(SubdivisionLevel);
			size_t idx = 0;
			vertices.resize(numVerts);
			for (float y = 0.0f; y < static_cast<float>(count2); ++y) {
				for (float x = 0.0f; x < static_cast<float>(count2); ++x) {
					vertices.positions[idx] = glm::vec3(x * scale, height_nodes->GetHeight(GridPos.z, glm::vec2(x * scale, y * scale)), y * scale);
					++idx;
				}
			}

			idx = 0;
			indices.resize(numTris + 1);
			for (size_t y = 0; y < SubdivisionLevel; ++y) {
				for (size_t x = 0; x < SubdivisionLevel; ++x) {
					indices[idx] = static_cast<uint32_t>((y * count2) + x);
					indices[idx + 1] = static_cast<uint32_t>(((y + 1) * count2) + x);
					indices[idx + 2] = static_cast<uint32_t>((y * count2) + x + 1);
					indices[idx + 3] = static_cast<uint32_t>(((y + 1) * count2) + x);
					indices[idx + 4] = static_cast<uint32_t>(((y + 1) * count2) + x + 1);
					indices[idx + 5] = static_cast<uint32_t>((y * count2) + x + 1);
					idx += 6;
				}
			}
			indices.shrink_to_fit();
			vertices.shrink_to_fit();
		}
		
	}
}