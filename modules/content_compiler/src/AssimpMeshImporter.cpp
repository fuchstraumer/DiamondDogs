#include "MeshData.hpp"
#include "glm/vec3.hpp"
#include "glm/vec4.hpp"
#include "glm/gtc/quaternion.hpp"
#include "glm/gtc/type_precision.hpp"
#include "assimp/Importer.hpp"
#include "assimp/postprocess.h"
#include "assimp/cimport.h"
#include "assimp/scene.h"
#include "easylogging++.h"
#include <vector>
#include <stdexcept>
#include <memory>
#include <vulkan/vulkan.h>
#include <string>

constexpr static const uint32_t LOADER_VERSION = 0x00000002;

glm::i16vec4 packSnorm16(glm::vec4 v) noexcept {
    auto packSnorm16_single = [](float v)->int16_t {
        return static_cast<int16_t>(std::roundf(glm::clamp(v, -1.0f, 1.0f) * 32767.0f));
    };
    return glm::i16vec4{
        packSnorm16_single(v.x),
        packSnorm16_single(v.y),
        packSnorm16_single(v.z),
        packSnorm16_single(v.w)
    };
}

static glm::quat pack_tangent_frame(const glm::mat3& m, size_t storage_size = sizeof(int16_t)) {

    glm::quat q = glm::quat_cast(glm::mat3{ m[0], glm::cross(m[2], m[0]), m[2] });
    q = glm::normalize(q);
    q = q.w < 0.0f ? -q : q;

    const float bias = 1.0f / static_cast<float>((1 << (storage_size * CHAR_BIT - 1)) - 1);
    if (q.w < bias) {
        q.w = bias;
        const float factor = std::sqrtf(1.0f - bias * bias);
        q.x *= factor;
        q.y *= factor;
        q.z *= factor;
    }

    if (glm::dot(glm::cross(m[0], m[2]), m[1]) < 0.0f) {
        q = -q;
    }

    return q;
}

struct vertex_t {
    vertex_t(const glm::vec3& p, const glm::quat& _tangents, const glm::vec3& _uv0) : tangents(packSnorm16(glm::vec4(_tangents.x, _tangents.y, _tangents.z, _tangents.w))) {
        position = fvec4(p.x, p.y, p.z, 1.0f);
        uv0 = fvec2(_uv0.x, _uv0.y);
    }
    fvec4 position;
    glm::i16vec4 tangents;
    fvec2 uv0;
};

struct uninterleaved_data_t {
    std::vector<decltype(vertex_t::position)> positions;
    std::vector<decltype(vertex_t::tangents)> tangents;
    std::vector<decltype(vertex_t::uv0)> uv0s;
    std::vector<decltype(vertex_t::uv0)> uv1s;
};

struct imported_aabb {
    glm::vec3 center;
    glm::vec3 half_extent;
    imported_aabb& set(const glm::vec3& min, const glm::vec3& max) {
        center = (min + max) * 0.5f;
        half_extent = (max - min) * 0.5f;
        return *this;
    }
    glm::vec3 min() const noexcept {
        return center - half_extent;
    }
    glm::vec3 max() const noexcept {
        return center + half_extent;
    }
};

template<typename VECTOR, typename INDEX>
static imported_aabb compute_aabb(VECTOR const* positions, INDEX const* indices, const size_t count, const size_t stride) noexcept {
    glm::vec3 bmin(std::numeric_limits<float>::max());
    glm::vec3 bmax(std::numeric_limits<float>::lowest());
    for (size_t i = 0; i < count; ++i) {
        VECTOR const* p = reinterpret_cast<VECTOR const*>(
            (char const*)positions + indices[i] * stride
        );
        const glm::vec3 v(p->x(), p->y(), p->z());
        bmin = glm::min(bmin, v);
        bmax = glm::max(bmax, v);
    }
    return imported_aabb{
        (bmin + bmax) * 0.5f,
        (bmax - bmin) * 0.5f
    };
}

struct SingleMesh {
    SingleMesh(uint32_t _offset, uint32_t _count, uint32_t min_index, uint32_t max_index, uint32_t _material,
        imported_aabb _aabb) : offset(_offset), count(_count), minIndex(min_index), maxIndex(max_index),
        material(_material), aabb(_aabb) {}
    uint32_t offset;
    uint32_t count;
    uint32_t minIndex;
    uint32_t maxIndex;
    uint32_t material;
    imported_aabb aabb;
};

struct mesh_conversion_job_data_t {
    std::vector<vertex_t> g_vertices; 
    std::vector<uint32_t> g_indices;
    std::vector<SingleMesh> meshes;
    uninterleaved_data_t g_data;
    size_t numVertices{ 0 };
};


template<bool INTERLEAVED>
void process_ainode(const aiScene* scene, const aiNode* node, mesh_conversion_job_data_t* job_data) {
    job_data->meshes.reserve(job_data->meshes.size() + node->mNumMeshes);

    for (size_t i = 0; i < node->mNumMeshes; ++i) {
        const aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
        if (!mesh->HasNormals()) {
            LOG(ERROR) << "Tried to load a mesh that has no normals: these are required for any mesh that will be loaded!";
            throw std::runtime_error("Mesh lacks normals!");
        }

        if (!mesh->HasTextureCoords(0)) {
            LOG(ERROR) << "Mesh lacks texture coordinates! These are required for any mesh that will be loaded through the asset pipeline.";
            throw std::runtime_error("Mesh lacks texture coordinates!");
        }

        const glm::vec3* vertices = reinterpret_cast<const glm::vec3*>(mesh->mVertices);
        const glm::vec3* tangents = reinterpret_cast<const glm::vec3*>(mesh->mTangents);
        const glm::vec3* bitangents = reinterpret_cast<const glm::vec3*>(mesh->mBitangents);
        const glm::vec3* normals = reinterpret_cast<const glm::vec3*>(mesh->mNormals);
        const glm::vec3* uv0 = reinterpret_cast<const glm::vec3*>(mesh->mTextureCoords[0]);
        const glm::vec3* uv1 = reinterpret_cast<const glm::vec3*>(mesh->mTextureCoords[1]);

        if (mesh->HasVertexColors(0)) {
            LOG(WARNING) << "Mesh uses per-vertex colors, which are unsupported!";
        }

        if (!mesh->HasTextureCoords(1)) {
            uv1 = nullptr;
        }

        const size_t num_vertices = mesh->mNumVertices;
        if (num_vertices > 0) {

            const aiFace* faces = mesh->mFaces;
            const size_t num_faces = mesh->mNumFaces;

            if (num_faces > 0) {

                const size_t vertex_offset = job_data->numVertices;
                job_data->numVertices += num_vertices;

                if (INTERLEAVED) {
                    job_data->g_vertices.reserve(job_data->g_vertices.size() + num_vertices);
                }
                else {
                    job_data->g_data.positions.reserve(job_data->g_data.positions.size() + num_vertices);
                    job_data->g_data.tangents.reserve(job_data->g_data.tangents.size() + num_vertices);
                    job_data->g_data.uv0s.reserve(job_data->g_data.uv0s.size() + num_vertices);
                }

                for (size_t j = 0; j < num_vertices; ++j) {
                    glm::quat q = pack_tangent_frame(glm::mat3(tangents[j], bitangents[j], normals[j]));
                    if (INTERLEAVED) {
                        job_data->g_vertices.emplace_back(vertices[j], q, uv0[j]);
                    }
                    else {
                        vertex_t v(vertices[j], q, uv0[j]);
                        job_data->g_data.positions.emplace_back(v.position);
                        job_data->g_data.tangents.emplace_back(v.tangents);
                        job_data->g_data.uv0s.emplace_back(v.uv0);
                        if (uv1 != nullptr) {
                            job_data->g_data.uv1s.emplace_back(uv1[j].x, uv1[j].y);
                        }
                    }
                }


                const size_t indices_count = num_faces * faces[0].mNumIndices;
                const size_t index_buffer_offset = job_data->g_indices.size();
                job_data->g_indices.reserve(job_data->g_indices.size() + indices_count);

                for (size_t j = 0; j < num_faces; ++j) {
                    const aiFace& face = faces[j];
                    for (size_t k = 0; k < face.mNumIndices; ++k) {
                        job_data->g_indices.emplace_back(uint32_t(face.mIndices[k] + vertex_offset));
                    }
                }

                const size_t stride = INTERLEAVED ? sizeof(vertex_t) : sizeof(vertex_t::position);
                const decltype(vertex_t::position)* positions = INTERLEAVED ? &job_data->g_vertices.data()->position : job_data->g_data.positions.data();
                const imported_aabb aabb(compute_aabb(positions, job_data->g_indices.data() + index_buffer_offset, indices_count, stride));
                job_data->meshes.emplace_back(uint32_t(index_buffer_offset), uint32_t(indices_count), uint32_t(vertex_offset), uint32_t(vertex_offset + indices_count - 1), uint32_t(mesh->mMaterialIndex), aabb);

            }
        }
    }

    for (size_t i = 0; i < node->mNumChildren; ++i) {
        process_ainode<INTERLEAVED>(scene, node->mChildren[i], job_data);
    }

    job_data->g_vertices.shrink_to_fit();
    job_data->g_indices.shrink_to_fit();
    job_data->g_data.positions.shrink_to_fit();
    job_data->g_data.tangents.shrink_to_fit();
    job_data->g_data.uv0s.shrink_to_fit();
    job_data->g_data.uv1s.shrink_to_fit();
    job_data->meshes.shrink_to_fit();

}

std::unique_ptr<mesh_conversion_job_data_t> loadInterleaved(const aiScene* scene, const aiNode* root_node) { 
    std::unique_ptr<mesh_conversion_job_data_t> job_data = std::make_unique<mesh_conversion_job_data_t>();
    process_ainode<true>(scene, root_node, job_data.get());
    return job_data;
}

std::unique_ptr<mesh_conversion_job_data_t> loadUninterleaved(const aiScene* scene, const aiNode* root_node) {
    std::unique_ptr<mesh_conversion_job_data_t> job_data = std::make_unique<mesh_conversion_job_data_t>();
    process_ainode<false>(scene, root_node, job_data.get());
    return job_data;
}

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning( disable : 4996 )
#endif

MeshData* AssimpLoadMeshData(const char * fname, MeshProcessingOptions* opts) {
    using namespace Assimp;
    Importer importer;
    importer.SetPropertyInteger(AI_CONFIG_PP_SBP_REMOVE, aiPrimitiveType_LINE | aiPrimitiveType_POINT);
    importer.SetPropertyBool(AI_CONFIG_IMPORT_COLLADA_IGNORE_UP_DIRECTION, true);
    importer.SetPropertyBool(AI_CONFIG_PP_PTV_KEEP_HIERARCHY, true);

    const aiScene* scene = importer.ReadFile(fname,
        aiProcess_GenSmoothNormals | aiProcess_CalcTangentSpace |
        aiProcess_FindInstances | aiProcess_OptimizeMeshes |
        aiProcess_JoinIdenticalVertices | aiProcess_ImproveCacheLocality |
        aiProcess_PreTransformVertices | aiProcess_SortByPType | aiProcess_Triangulate
    );

    if (!scene) {
        LOG(ERROR) << "Failed to load scene from file " << fname << "! File either doesn't exist or loading failed in-progress";
        return nullptr;
    }

    std::unique_ptr<mesh_conversion_job_data_t> data = nullptr;

    if (opts->Interleaved) {
        data = loadInterleaved(scene, scene->mRootNode);
    }
    else {
        data = loadUninterleaved(scene, scene->mRootNode);
    }

    const bool has_uv1 = data->g_data.uv1s.size() > 0;
    const bool has_index_16 = (data->numVertices < std::numeric_limits<uint16_t>::max()) && (opts->AllowInt16_Indices);

    auto union_aabb = [](imported_aabb& dest, const imported_aabb& other) {
        dest.set(glm::min(dest.min(), other.min()), glm::max(dest.max(), other.max()));
    };

    imported_aabb total_aabb;
    static const glm::vec3 starting_min(std::numeric_limits<float>::max());
    static const glm::vec3 starting_max(std::numeric_limits<float>::lowest());
    total_aabb.set(starting_min, starting_max);

    for (auto& mesh : data->meshes) {
        union_aabb(total_aabb, mesh.aabb);
    }

    MeshDataHeader header = MeshDataHeader();
    header.Version = LOADER_VERSION;
    header.Center[0] = total_aabb.center.x;
    header.Center[1] = total_aabb.center.y;
    header.Center[2] = total_aabb.center.z;
    header.HalfExtent[0] = total_aabb.half_extent.x;
    header.HalfExtent[1] = total_aabb.half_extent.y;
    header.HalfExtent[2] = total_aabb.half_extent.z;
    header.NumParts = static_cast<uint32_t>(data->meshes.size());
    header.Interleaved = uint32_t(opts->Interleaved);
    header.PositionAttrFormat = uint32_t(VK_FORMAT_R32G32B32A32_SFLOAT);
    header.TangentAttrFormat = uint32_t(VK_FORMAT_R16G16B16A16_SNORM);
    header.UV0_Format = uint32_t(VK_FORMAT_R32G32_SFLOAT);

    if (opts->Interleaved) {
        header.PositionAttrOffset = offsetof(vertex_t, position);
        header.TangentAttrOffset = offsetof(vertex_t, tangents);
        header.UV0_Offset = offsetof(vertex_t, uv0);
        header.PositionAttrStride = sizeof(vertex_t);
        header.TangentAttrStride = sizeof(vertex_t);
        header.UV0_Stride = sizeof(vertex_t);
    }
    else {
        header.PositionAttrOffset = 0;
        header.TangentAttrOffset = static_cast<uint32_t>(data->numVertices * sizeof(vertex_t::position));
        header.UV0_Offset = static_cast<uint32_t>(header.TangentAttrOffset + data->numVertices * sizeof(vertex_t::tangents));
        header.PositionAttrStride = sizeof(vertex_t::position);
        header.TangentAttrStride = sizeof(vertex_t::tangents);
        header.UV0_Stride = sizeof(vertex_t::uv0);
        if (has_uv1) {
            header.UV1_Offset = static_cast<uint32_t>(header.UV0_Offset + data->numVertices * sizeof(vertex_t::uv0));
            header.UV1_Stride = sizeof(vertex_t::uv0);
            header.UV1_Format = uint32_t(VK_FORMAT_R32G32_SFLOAT);
        }
    }

    header.VertexCount = static_cast<uint32_t>(data->numVertices);
    header.IndexFormat = has_index_16 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
    header.IndexCount = static_cast<uint32_t>(data->g_indices.size());
    header.IndexDataSize = static_cast<uint32_t>(data->g_indices.size() * (has_index_16 ? sizeof(uint16_t) : sizeof(uint32_t)));

    MeshData* result = new MeshData();
    result->Header = header;

    result->Parts = new PartData[data->meshes.size()];
    static_assert(sizeof(PartData) == sizeof(SingleMesh), "Sizes of part data and single mesh must match");
    // objects are exact same in raw memory
    memcpy(result->Parts, data->meshes.data(), sizeof(PartData) * data->meshes.size());

    result->Header.NumMaterials = scene->mNumMaterials;
    result->Materials = new MaterialInfo[scene->mNumMaterials];
    for (uint32_t i = 0; i < result->Header.NumMaterials; ++i) {
        const aiMaterial* mat = scene->mMaterials[i];
        aiString name;
        if (mat->Get(AI_MATKEY_NAME, name) != AI_SUCCESS) {
            LOG(WARNING) << "Unnamed material replaced with default material.";
            result->Materials[i].NameLength = uint32_t(7);
            result->Materials[i].Name = new char[8];
            constexpr const char* const default_str = "Default\0";

            strncpy(result->Materials[i].Name, default_str, 8);
        }
        else {
            result->Materials[i].NameLength = uint32_t(name.length);
            result->Materials[i].Name = new char[name.length + 1];
            strncpy(result->Materials[i].Name, name.C_Str(), name.length);
            result->Materials[i].Name[name.length] = '\0';
        }
    }

    if (has_index_16) {
        result->Indices = new uint16_t[data->g_indices.size()];
        uint16_t* output_indices = reinterpret_cast<uint16_t*>(result->Indices);
        for (size_t i = 0; i < data->g_indices.size(); ++i) {
            output_indices[i] = static_cast<uint16_t>(data->g_indices[i]);
        }
    }
    else {
        result->Indices = new uint32_t[data->g_indices.size()];
        memcpy(result->Indices, data->g_indices.data(), sizeof(uint32_t) * data->g_indices.size());
    }

    if (opts->Interleaved) {
        result->Vertices = std::vector<ccVertex>(data->g_vertices.size());
        static_assert(sizeof(vertex_t) == sizeof(ccVertex), "Size of vertex_t and public-facing ccVertex types must be the same!");
        memcpy(result->Vertices, data->g_vertices.data(), data->g_vertices.size() * sizeof(vertex_t));
    }
    else {
        result->Vertices = new UninterleavedVertexData();
        UninterleavedVertexData* vertex_data = reinterpret_cast<UninterleavedVertexData*>(result->Vertices);
        vertex_data->Positions = new fvec4[data->g_data.positions.size()];
        memcpy(vertex_data->Positions, data->g_data.positions.data(), sizeof(fvec4) * data->g_data.positions.size());
        vertex_data->Tangents = new int16_t[data->g_data.tangents.size() * 4];
        memcpy(vertex_data->Tangents, data->g_data.tangents.data(), sizeof(glm::i16vec4) * data->g_data.tangents.size());
        vertex_data->UV0s = new fvec2[data->g_data.uv0s.size()];
        memcpy(vertex_data->UV0s, data->g_data.uv0s.data(), sizeof(fvec2) * data->g_data.uv0s.size());
        if (has_uv1) {
            vertex_data->UV1s = new fvec2[data->g_data.uv1s.size() * 2];
            memcpy(vertex_data->UV1s, data->g_data.uv1s.data(), sizeof(fvec2) * data->g_data.uv1s.size());
        }
    }

    return result;
}

#ifdef _MSC_VER
#pragma warning(pop)
#endif //!_MSC_VER 
