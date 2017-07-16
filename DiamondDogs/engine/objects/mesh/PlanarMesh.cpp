#include "stdafx.h"
#include "PlanarMesh.h"

namespace vulpes {
	namespace mesh {
		PlanarMesh::PlanarMesh(const double& side_length, const glm::ivec3& grid_pos, const glm::vec3& pos, const glm::vec3& scale, const glm::vec3& angle) : Mesh(pos, scale, angle), SideLength(side_length), GridPos(grid_pos), SubdivisionLevel(grid_pos.z) {}

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

		void PlanarMesh::Generate(terrain::HeightNode* height_node) {
			SubdivisionLevel = height_node->MeshGridSize();

			size_t count2 = SubdivisionLevel + 1;
			size_t numTris = SubdivisionLevel*SubdivisionLevel * 6;
			size_t numVerts = count2*count2;
			float grid_scale = static_cast<float>(SideLength) / static_cast<float>(SubdivisionLevel);
			size_t idx = 0;
			vertices.resize(numVerts);
			
			// Place vertices and set normal to 0.0f
			for (float y = 0.0f; y < static_cast<float>(count2); ++y) {
				for (float x = 0.0f; x < static_cast<float>(count2); ++x) {
					vertices.positions[idx] = glm::vec3(x * grid_scale, height_node->GetHeight(glm::vec2(x * grid_scale, y * grid_scale)), y * grid_scale);
					vertices.normals_uvs[idx].normal = glm::vec3(0.0f);
					++idx;
				}
			}

			idx = 0;
			indices.resize(numTris + 1);
			for (size_t y = 0; y < SubdivisionLevel; ++y) {
				for (size_t x = 0; x < SubdivisionLevel; ++x) {
					/*
						wrong diagonal: flipped as required by Proland's sampling system
						https://proland.inrialpes.fr/doc/proland-4.0/terrain/html/index.html -> See sec. 2.2.2 
						"note: the mesh diagonal must be oriented from "north west" to "south east""
						indices[idx] = static_cast<uint32_t>((y * count2) + x);
						indices[idx + 1] = static_cast<uint32_t>((y  * count2) + x + 1);
						indices[idx + 2] = static_cast<uint32_t>(((y + 1) * count2) + x);
					*/
					indices[idx] = static_cast<uint32_t>((y  * count2) + x + 1);
					indices[idx + 1] = static_cast<uint32_t>(((y + 1) * count2) + x + 1);
					indices[idx + 2] = static_cast<uint32_t>((y * count2) + x);
					{
						// Generate normals for this triangle.
						const glm::vec3 edge0 = vertices.positions[indices[idx]] - vertices.positions[indices[idx + 1]];
						const glm::vec3 edge1 = vertices.positions[indices[idx + 2]] - vertices.positions[indices[idx + 1]];
						glm::vec3 normal = glm::cross(edge0, edge1);
						vertices.normals_uvs[indices[idx]].normal += normal;
						vertices.normals_uvs[indices[idx + 1]].normal += normal;
						vertices.normals_uvs[indices[idx + 2]].normal += normal;
					}
					/*
						wrong diagonal
						indices[idx + 3] = static_cast<uint32_t>(((y + 1) * count2) + x);
						indices[idx + 4] = static_cast<uint32_t>((y * count2) + x + 1);
						indices[idx + 5] = static_cast<uint32_t>(((y + 1) * count2) + x + 1);
					*/
					indices[idx + 3] = static_cast<uint32_t>((y * count2) + x);
					indices[idx + 4] = static_cast<uint32_t>(((y + 1) * count2) + x + 1);
					indices[idx + 5] = static_cast<uint32_t>(((y + 1) * count2) + x);
					
					{
						// Next set of normals.
						const glm::vec3 edge0 = vertices.positions[indices[idx + 3]] - vertices.positions[indices[idx + 4]];
						const glm::vec3 edge1 = vertices.positions[indices[idx + 5]] - vertices.positions[indices[idx + 4]];
						glm::vec3 normal = glm::cross(edge0, edge1);
						vertices.normals_uvs[indices[idx + 3]].normal += normal;
						vertices.normals_uvs[indices[idx + 4]].normal += normal;
						vertices.normals_uvs[indices[idx + 5]].normal += normal;
					}
					idx += 6;
				}
			}

			// normalize each normal vector, otherwise normal vectors don't behave like normals.
			for (auto iter = vertices.normals_uvs.begin(); iter != vertices.normals_uvs.end(); ++iter) {
				iter->normal = glm::normalize(iter->normal);
			}

			indices.shrink_to_fit();
			vertices.shrink_to_fit();
		}
		
	}
}