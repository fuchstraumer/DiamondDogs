#include "objects/MeshPrimitives.hpp"
#include "resources/MeshData.hpp"
#include <array>
#include <vector>
#include "glm/glm.hpp"
namespace vpsk {

    static const std::array<glm::vec3, 8> BOX_POSITIONS {
        glm::vec3(-1.0f, -1.0f, +1.0f), 
        glm::vec3(+1.0f, -1.0f, +1.0f), 
        glm::vec3(+1.0f, +1.0f, +1.0f), 
        glm::vec3(-1.0f, +1.0f, +1.0f),
        glm::vec3(+1.0f, -1.0f, -1.0f), 
        glm::vec3(-1.0f, -1.0f, -1.0f), 
        glm::vec3(-1.0f, +1.0f, -1.0f), 
        glm::vec3(+1.0f, +1.0f, -1.0f),
    };
    
    static const std::array<glm::vec3, 6> BOX_NORMALS {
        glm::vec3( 0.0f, 0.0f, 1.0f),
        glm::vec3( 1.0f, 0.0f, 0.0f),
        glm::vec3( 0.0f, 1.0f, 0.0f),
        glm::vec3(-1.0f, 0.0f, 0.0f),
        glm::vec3( 0.0f,-1.0f, 0.0f),
        glm::vec3( 0.0f, 0.0f,-1.0f)
    };

    static const std::array<glm::vec3, 4> BOX_UVS {
        glm::vec3( 0.0f, 0.0f, 0.0f ),
        glm::vec3( 1.0f, 0.0f, 0.0f ),
        glm::vec3( 1.0f, 1.0f, 0.0f ),
        glm::vec3( 0.0f, 1.0f, 0.0f )
    };

    static constexpr float GOLDEN_RATIO = 1.61803398875f;
    static constexpr float FLOAT_PI = 3.141592653589793238462f;

    static const std::array<glm::vec3, 12> ICOSPHERE_POSITIONS {
        glm::vec3(-GOLDEN_RATIO, 1.0f, 0.0f),
        glm::vec3( GOLDEN_RATIO, 1.0f, 0.0f),
        glm::vec3(-GOLDEN_RATIO,-1.0f, 0.0f),
        glm::vec3( GOLDEN_RATIO,-1.0f, 0.0f),
        glm::vec3( 0.0f,-GOLDEN_RATIO, 1.0f),
        glm::vec3( 0.0f, GOLDEN_RATIO, 1.0f),
        glm::vec3( 0.0f,-GOLDEN_RATIO,-1.0f),
        glm::vec3( 0.0f, GOLDEN_RATIO,-1.0f),
        glm::vec3( 1.0f, 0.0f,-GOLDEN_RATIO),
        glm::vec3( 1.0f, 0.0f, GOLDEN_RATIO),
        glm::vec3(-1.0f, 0.0f,-GOLDEN_RATIO),
        glm::vec3(-1.0f, 0.0f, GOLDEN_RATIO)
    };

    constexpr static std::array<uint32_t, 60> ICOSPHERE_INDICES {
        0,11, 5, 0, 5, 1,
        0, 1, 7, 0, 7,10,
        0,10,11, 5,11, 4,
        1, 5, 9, 7, 1, 8,
       10, 7, 6,11,10, 2,
        3, 9, 4, 3, 4, 2,
        3, 2, 6, 3, 6, 8,
        3, 8, 9, 4, 9, 5,
        2, 4,11, 6, 2,10,
        8, 6, 7, 9, 8, 1
    };

    std::unique_ptr<MeshData> CreateBox(const ExtraFeatures features) {
        std::unique_ptr<MeshData> result = std::make_unique<MeshData>();

        struct vertex_t {
            glm::vec3 Position;
            vertex_data_t Data;
        };

        auto get_face_vertices = [&](const size_t& face_idx, vertex_t& v0, vertex_t& v1, vertex_t& v2,
            vertex_t& v3) {
            switch (face_idx) {
            case 0:
                v0.Position = BOX_POSITIONS[0];
                v1.Position = BOX_POSITIONS[1];
                v2.Position = BOX_POSITIONS[2];
                v3.Position = BOX_POSITIONS[3];
                break;
            case 1:
                v0.Position = BOX_POSITIONS[1];
                v1.Position = BOX_POSITIONS[4];
                v2.Position = BOX_POSITIONS[7];
                v3.Position = BOX_POSITIONS[2];
                break;
            case 2:
                v0.Position = BOX_POSITIONS[3];
                v1.Position = BOX_POSITIONS[2];
                v2.Position = BOX_POSITIONS[7];
                v3.Position = BOX_POSITIONS[6];
                break;
            case 3:
                v0.Position = BOX_POSITIONS[5];
                v1.Position = BOX_POSITIONS[0];
                v2.Position = BOX_POSITIONS[3];
                v3.Position = BOX_POSITIONS[6];
                break;
            case 4:
                v0.Position = BOX_POSITIONS[5];
                v1.Position = BOX_POSITIONS[4];
                v2.Position = BOX_POSITIONS[1];
                v3.Position = BOX_POSITIONS[0];
                break;
            case 5:
                v0.Position = BOX_POSITIONS[4];
                v1.Position = BOX_POSITIONS[5];
                v2.Position = BOX_POSITIONS[6];
                v3.Position = BOX_POSITIONS[7];
                break;
            default:
                throw std::domain_error("Invalid face_idx");
            }

            v0.Data.Normal = v1.Data.Normal = v2.Data.Normal = v3.Data.Normal = BOX_NORMALS[face_idx];
            v0.Data.UV = BOX_UVS[0];
            v1.Data.UV = BOX_UVS[1];
            v2.Data.UV = BOX_UVS[2];
            v3.Data.UV = BOX_UVS[3];
        };

        auto add_vertex = [&result](vertex_t&& vert)->uint32_t {
            const uint32_t idx = static_cast<uint32_t>(result->Positions.size());
            result->Positions.emplace_back(std::move(vert.Position));
            result->Vertices.emplace_back(std::move(vert.Data));
            return idx;
        };

        auto build_face = [&](const size_t& face_idx) {
            vertex_t v0, v1, v2, v3;
            get_face_vertices(face_idx, v0, v1, v2, v3);
            result->Indices.emplace_back(add_vertex(std::move(v0)));
            result->Indices.emplace_back(add_vertex(std::move(v1)));
            result->Indices.emplace_back(add_vertex(std::move(v2)));
            result->Indices.emplace_back(add_vertex(std::move(v3)));
        };
        
        for (size_t i = 0; i < 6; ++i) {
            build_face(i);
        }

        if (features == ExtraFeatures::GenerateTangents) {
            GenerateTangentVectors(result.get());
        }

        return std::move(result);
    }

    std::unique_ptr<MeshData> CreateIcosphere(const size_t detail_level, const ExtraFeatures features) {
        
        std::unique_ptr<MeshData> result = std::make_unique<MeshData>();
        result->Indices.assign(ICOSPHERE_INDICES.cbegin(), ICOSPHERE_INDICES.cend());
        result->Positions.assign(ICOSPHERE_POSITIONS.cbegin(), ICOSPHERE_POSITIONS.cend());
        for (const auto& vert : ICOSPHERE_POSITIONS) {
            result->Vertices.emplace_back(vertex_data_t{ vert, glm::vec3(0.0f), glm::vec3(0.0f) });
        }

        for (size_t i = 0; i < detail_level; ++i) {
            const size_t num_triangles = result->Indices.size();

            for (size_t j = 0; j < num_triangles; ++j) {
                const uint32_t i0 = result->Indices[j * 3 + 0];
                const uint32_t i1 = result->Indices[j * 3 + 1];
                const uint32_t i2 = result->Indices[j * 3 + 2];

                const uint32_t i3 = static_cast<uint32_t>(result->Positions.size());
                const uint32_t i4 = i3 + 1;
                const uint32_t i5 = i4 + 1;

                result->Indices[j * 3 + 1] = i3;
                result->Indices[j * 3 + 2] = i5;

                result->Indices.insert(result->Indices.end(), { i3, i1, i4, i5, i3, i4, i5, i4, i2 });
                result->Positions.emplace_back(0.5f * (result->Positions[i0] + result->Positions[i1]));
                result->Vertices.emplace_back(vertex_data_t{ result->Positions.back(), glm::vec3(0.0f), glm::vec3(0.0f) });
                result->Positions.emplace_back(0.5f * (result->Positions[i1] + result->Positions[i2]));
                result->Vertices.emplace_back(vertex_data_t{ result->Positions.back(), glm::vec3(0.0f), glm::vec3(0.0f) });
                result->Positions.emplace_back(0.5f * (result->Positions[i2] + result->Positions[i0]));
                result->Vertices.emplace_back(vertex_data_t{ result->Positions.back(), glm::vec3(0.0f), glm::vec3(0.0f) });
            }
        }

        for (size_t i = 0; i < result->Positions.size(); ++i) {
            result->Positions[i] = glm::normalize(result->Positions[i]);
            result->Vertices[i].Normal = glm::normalize(result->Vertices[i].Normal);
        }

        for(size_t i = 0; i < result->Positions.size(); ++i) {
            const glm::vec3& norm = result->Vertices[i].Normal;
            result->Vertices[i].UV.x = (glm::atan((float)norm.x, -norm.z) / FLOAT_PI) * 0.5f + 0.5f;
            result->Vertices[i].UV.y = -norm.y * 0.5f + 0.5f;
        }

        auto add_vertex_w_uv = [&](const size_t& i, const glm::vec3& uv) {
            const uint32_t idx = result->Indices[i];
            result->Vertices[idx].UV = uv;
        };

        const size_t num_triangles = result->Indices.size() / 3;
        auto& vertexData = result->Vertices;
        auto& indices = result->Indices;

        for(size_t i = 0; i < num_triangles; ++i) {
            const glm::vec3& uv0 = vertexData[indices[i * 3]].UV;
            const glm::vec3& uv1 = vertexData[indices[i * 3 + 1]].UV;
            const glm::vec3& uv2 = vertexData[indices[i * 3 + 2]].UV;
            const float d1 = uv1.x - uv0.x;
            const float d2 = uv2.x - uv0.x;
            if(std::abs(d1) > 0.5f && std::abs(d2) > 0.5f){
                add_vertex_w_uv(i * 3, uv0 + glm::vec3((d1 > 0.0f) ? 1.0f : -1.0f, 0.0f, 0.0f));
            }
            else if(std::abs(d1) > 0.5f) {
                add_vertex_w_uv(i * 3 + 1, uv1 + glm::vec3((d1 < 0.0f) ? 1.0f : -1.0f, 0.0f, 0.0f));
            }
            else if(std::abs(d2) > 0.5f) {
                add_vertex_w_uv(i * 3 + 2, uv2 + glm::vec3((d2 < 0.0f) ? 1.0f : -1.0f, 0.0f, 0.0f));
            }
        }
        
        if (features == ExtraFeatures::GenerateTangents) {
            GenerateTangentVectors(result.get());
        }

        return std::move(result);
    }

    void GenerateTangentVectors(MeshData* mesh) {
        // From https://github.com/mlimper/tgen/
        const size_t num_corners = mesh->Indices.size();

        glm::vec3 edge3D[3];
        glm::vec2 edgeUV[3];

        for (size_t i = 0; i < num_corners; i += 3) {
            const uint32_t indices[3] = { mesh->Indices[i + 0], 
                                          mesh->Indices[i + 1],
                                          mesh->Indices[i + 2] };
            
            for (size_t j = 0; j < 3; ++j) {
                const size_t next = (j + 1) % 3;
                const uint32_t& v0_idx = indices[j];
                const uint32_t& v1_idx = indices[next];

                edge3D[j] = mesh->Positions[v1_idx] - mesh->Positions[v0_idx];
                edgeUV[j] = mesh->Vertices[v1_idx].UV - mesh->Vertices[v0_idx].UV;
            }

            for (size_t j = 0; j < 3; ++j) {
                const size_t prev = (j + 2) % 3;

                const glm::vec3& pos0 = edge3D[j];
                const glm::vec3& pos1 = edge3D[prev];
                const glm::vec2& uv0 = edgeUV[j];
                const glm::vec2& uv1 = edgeUV[prev];

                const float denom = (uv0.x * -uv1.y - uv0.y * -uv1.x);
                const float r = denom > std::numeric_limits<float>::epsilon() ? 1.0f / denom : 0.0f;

                glm::vec3 tmp0 = pos0 * (-uv1.y * r);
                glm::vec3 tmp1 = pos1 * (-uv0.y * r);
                mesh->Vertices[indices[j]].Tangent = tmp0 - tmp1;

                tmp0 = pos1 * (-uv0.x * r);
                tmp1 = pos0 * (-uv1.x * r);
                mesh->Vertices[indices[j]].Bitangent = tmp0 - tmp1;

            }
        }
    }

}
