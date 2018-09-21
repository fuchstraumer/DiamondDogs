#include "ObjModel.hpp"
#pragma warning(push, 0)
#include "glm/vec3.hpp"
#include "glm/vec2.hpp"
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/hash.hpp>
#define NOMINMAX
#define TINYOBJ_LOADER_OPT_IMPLEMENTATION 
#include <tinyobjloader/experimental/tinyobj_loader_opt.h>
#pragma warning(pop)
#include <fstream>
#include <unordered_map>

struct vertex_hash {
    size_t operator()(const LoadedObjModel::vertex_t& v) const noexcept {
        return (std::hash<glm::vec3>()(v.pos) ^ std::hash<glm::vec2>()(v.uv));
    }
};


LoadedObjModel::LoadedObjModel(const char * fname) {
    tinyobj_opt::attrib_t attrib;
    std::vector<tinyobj_opt::shape_t> shapes;
    std::vector<tinyobj_opt::material_t> materials;
    std::string err;

    std::string file_input;
    std::ifstream input_obj(fname);
    if (!input_obj.is_open()) {
        throw std::runtime_error("Err");
    }

    std::string loaded_data{ std::istreambuf_iterator<char>(input_obj), std::istreambuf_iterator<char>() };

    if (!tinyobj_opt::parseObj(&attrib, &shapes, &materials, loaded_data.data(), loaded_data.length(), tinyobj_opt::LoadOption())) {
        throw std::runtime_error(err);
    }

    std::unordered_map<vertex_t, uint32_t, vertex_hash> unique_vertices{};

    indices.reserve(attrib.indices.size());
    vertices.reserve(attrib.vertices.size());

    for (const auto& index : attrib.indices) {
        glm::vec3 pos {
            attrib.vertices[3 * index.vertex_index + 0],
            attrib.vertices[3 * index.vertex_index + 1],
            attrib.vertices[3 * index.vertex_index + 2]
        };

        glm::vec2 uv{
            attrib.texcoords[2 * index.texcoord_index + 0],
            1.0f - attrib.texcoords[2 * index.texcoord_index + 1]
        };

        vertex_t vert{ pos, uv };

        if (unique_vertices.count(vert) == 0) {
            unique_vertices[vert] = static_cast<uint32_t>(vertices.size());
            vertices.emplace_back(vert);
        }

        indices.emplace_back(unique_vertices[vert]);
        
    }

    vertices.shrink_to_fit();
    indices.shrink_to_fit();
}
