#pragma once
#ifndef TERRAIN_PLUGIN_PLANAR_MESH_HPP
#define TERRAIN_PLUGIN_PLANAR_MESH_HPP
#include "glm/vec3.hpp"
#include "glm/mat4x4.hpp"
#include <vector>

class HeightNode;

class PlanarMesh {
public:
    PlanarMesh(const float side_length, const glm::ivec3& grid_pos, const glm::vec3& pos = glm::vec3(0.0f), const glm::vec3& scale = glm::vec3(1.0f), const glm::vec3& angle = glm::vec3(0.0f));
    ~PlanarMesh();
    glm::mat4 ModelMatrix() const noexcept;

    void SetPosition(glm::vec3 p);
    void SetScale(glm::vec3 s);
    void SetRotation(glm::vec3 r);

    void Generate(HeightNode* height_nodes);
private:

    size_t subdivisionLevel;
    float sideLength;
    glm::ivec3 gridPos;

    std::vector<uint32_t> indices;
    struct vertex_t {
        glm::vec3 position;
        glm::vec3 normal;
    };
    std::vector<vertex_t> vertices;
    glm::vec3 position;
    glm::vec3 scale;
    glm::vec3 rotation;
};

#endif // !TERRAIN_PLUGIN_PLANAR_MESH_HPP
