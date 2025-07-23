#include "PlanarMesh.hpp"
#include "HeightNode.hpp"
#include <algorithm>
#include <execution>

PlanarMesh::PlanarMesh(const float side_length, const glm::ivec3& grid_pos, const glm::vec3& pos, const glm::vec3& _scale, const glm::vec3& angle) : position(pos), scale(_scale), rotation(angle), sideLength(side_length), gridPos(grid_pos), subdivisionLevel(grid_pos.z) {}

PlanarMesh::~PlanarMesh() {}

void PlanarMesh::Generate(HeightNode* height_node) {
    subdivisionLevel = height_node->MeshGridSize();

    size_t count2 = subdivisionLevel + 1;
    size_t numTris = subdivisionLevel*subdivisionLevel * 6;
    size_t numVerts = count2*count2;
    float grid_scale = static_cast<float>(sideLength) / static_cast<float>(subdivisionLevel);
    size_t idx = 0;
    vertices.resize(numVerts);
    
    // Place vertices and set normal to 0.0f
    for (float y = 0.0f; y < static_cast<float>(count2); ++y) {
        for (float x = 0.0f; x < static_cast<float>(count2); ++x) {
            vertices[idx].position = glm::vec3(x * grid_scale, height_node->GetHeight(glm::vec2(x * grid_scale, y * grid_scale)), y * grid_scale);
            ++idx;
        }
    }

    idx = 0;
    indices.resize(numTris + 1);
    for (size_t y = 0; y < subdivisionLevel; ++y) {
        for (size_t x = 0; x < subdivisionLevel; ++x) {
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
                const glm::vec3 edge0 = vertices[indices[idx]].position - vertices[indices[idx + 1]].position;
                const glm::vec3 edge1 = vertices[indices[idx + 2]].position - vertices[indices[idx + 1]].position;
                glm::vec3 normal = glm::cross(edge0, edge1);
                vertices[indices[idx]].normal += normal;
                vertices[indices[idx + 1]].normal += normal;
                vertices[indices[idx + 2]].normal += normal;
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
                const glm::vec3 edge0 = vertices[indices[idx + 3]].position - vertices[indices[idx + 4]].position;
                const glm::vec3 edge1 = vertices[indices[idx + 5]].position - vertices[indices[idx + 4]].position;
                glm::vec3 normal = glm::cross(edge0, edge1);
                vertices[indices[idx + 3]].normal += normal;
                vertices[indices[idx + 4]].normal += normal;
                vertices[indices[idx + 5]].normal += normal;
            }
            idx += 6;
        }
    }

    // normalize each normal vector, otherwise normal vectors don't behave like normals.
    std::for_each(std::execution::par_unseq, std::begin(vertices), std::end(vertices), [](vertex_t& vert) {
        vert.normal = glm::normalize(vert.normal);
    });

}
