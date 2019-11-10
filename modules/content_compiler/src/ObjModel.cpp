#include "ObjModel.hpp"
#include "LogicalDevice.hpp"
#include "PhysicalDevice.hpp"
#include "RenderingContext.hpp"
#include "ResourceContext.hpp"
#include "ResourceLoader.hpp"
#include <atomic>
#include <fstream>
#include <future>
#pragma warning(push, 0)
#include "easylogging++.h"
#define GLM_ENABLE_EXPERIMENTAL
#include "glm/gtx/hash.hpp"
#include "mango/filesystem/file.hpp"
#define TINYOBJ_LOADER_OPT_IMPLEMENTATION
#include "tinyobj_loader_opt.h"
#pragma warning(pop)

namespace std {
    /** Hash method ovverride so that we can use vulpes::vertex_t in standard library containers. Used with tinyobj to remove/avoid duplicated vertices. \ingroup Objects */
    template<>
    struct hash<vertex_t> {
        size_t operator()(const vertex_t& vert) const {
            return (hash<glm::vec3>()(vert.Position)) ^ (hash<glm::vec3>()(vert.Normal) << 1) ^ (hash<glm::vec2>()(vert.UV) << 4);
        }
    };
}

static std::atomic<size_t> objModelInstanceCount{ 0u };

ObjectModel::ObjectModel() {}

void ObjectModel::LoadModelFromFile(const char* model_filename, const char* material_directory)
{

    tinyobj_opt::attrib_t attrib;
    std::vector<tinyobj_opt::shape_t> shapes;
    std::vector<tinyobj_opt::material_t> materials;

    {
        mango::filesystem::File model_file(model_filename);
        mango::Memory model_memory = model_file;

        tinyobj_opt::LoadOption options;
        // some profiling/debugging tools might present only one thread for use iirc, this should make that safe
        options.req_num_threads = std::max(static_cast<int>(std::thread::hardware_concurrency() - 2u), (int)1);
        if (!tinyobj_opt::parseObj(&attrib, &shapes, &materials, (char*)model_memory, model_memory.size, options))
        {
            throw std::runtime_error("Failed to load .obj file!");
        }
    }

    numMaterials = materials.size();
    createMaterials(materials, material_directory);

    loadMeshes(shapes, attrib);
    generateIndirectDraws();

}

constexpr bool ObjectModel::Part::operator==(const Part& other) const noexcept
{
    return (idxCount == other.idxCount) && (material == other.material);
}

constexpr bool ObjectModel::Part::operator<(const Part& other) const noexcept
{
    // only relevant metric for comparing them, at least where this is used
    // spatial comparisons have to be done based on a relative point
    return startIdx < other.startIdx;
}

void ObjectModel::loadMeshes(const std::vector<tinyobj_opt::shape_t>& shapes, const tinyobj_opt::attrib_t& attrib)
{

    AABB modelAABB;

    size_t idx_offset{ 0u };

    struct material_part
    {
        // -2 : default invalid value
        // -1 : invalid value read from mesh data
        // rest : index into this->materials
        size_t indexStart{ 0u };
        size_t indexCount{ 0u };
        AABB bounds;
    };

    std::unordered_multimap<int, material_part> material_parts;

    struct model_data
    {
        std::vector<vertex_t> vertices;
        std::vector<uint32_t> indices;
        std::vector<material_part> parts;
        AABB bounds;
    };

    std::unordered_map<vertex_t, uint32_t> unique_vertices;

    for (size_t i = 0u; i < attrib.face_num_verts.size(); ++i)
    {
        assert(attrib.face_num_verts[i] == 3u);
        // material ID for current face
        const int materialId = attrib.material_ids[i];

        vertex_t triangleVertices[3]{};

        for (size_t j = 0u; j < 3u; ++j)
        {
            tinyobj_opt::index_t tinyobjIdx = attrib.indices[idx_offset + j];

            triangleVertices[j].Position = glm::vec3{
                attrib.vertices[3u * tinyobjIdx.vertex_index + 0u],
                attrib.vertices[3u * tinyobjIdx.vertex_index + 1u],
                attrib.vertices[3u * tinyobjIdx.vertex_index + 2u]
            };

            triangleVertices[j].Normal = glm::vec3{
                attrib.normals[3u * tinyobjIdx.normal_index + 0u],
                attrib.normals[3u * tinyobjIdx.normal_index + 1u],
                attrib.normals[3u * tinyobjIdx.normal_index + 2u]
            };

            triangleVertices[j].UV = glm::vec2{
                attrib.texcoords[2u * tinyobjIdx.texcoord_index + 0],
                1.0f - attrib.texcoords[2u * tinyobjIdx.texcoord_index + 1u]
            };

        }

        // triangle vertices collected

        idx_offset += 3u;
    }

    //for (size_t i = 0u; i < attributes.size(); ++i)
    //{
    //    //const glm::vec3& normal = attributes[i].normal;
    //    //const glm::vec3& tan0 = vertexTangents.at(static_cast<uint32_t>(i));
    //    //attributes[i].tangent = glm::normalize(tan0 - normal * glm::dot(normal, tan0));
    //}

}
