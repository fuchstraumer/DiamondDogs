#pragma once
#ifndef RESOURCE_CONTEXT_OBJ_MODEL_HPP
#define RESOURCE_CONTEXT_OBJ_MODEL_HPP
#include "glm/vec3.hpp"
#include "glm/vec2.hpp"
#include <vector>

struct LoadedObjModel {
    LoadedObjModel(const char* fname);
    struct vertex_t {
        bool operator==(const vertex_t& other) const noexcept {
            return (pos == other.pos) && (uv == other.uv);
        }
        glm::vec3 pos;
        glm::vec2 uv;
    };
    std::vector<vertex_t> vertices;
    std::vector<uint32_t> indices;
};

#endif //!RESOURCE_CONTEXT_OBJ_MODEL_HPP
