#pragma once
#ifndef TERRAIN_PLUGIN_QUADTREE_HPP
#define TERRAIN_PLUGIN_QUADTREE_HPP
#include "Entity.hpp"

class HeightNode;
class TerrainNode;

struct QuadtreeInstance;
struct QuadtreeNodeComponent;

struct QuadtreeConfiguration
{
    size_t MaxLOD;
};

class QuadtreeSystem
{
public:

    static QuadtreeInstance* CreateQuadtreeInstance(const QuadtreeConfiguration& config);
    
    static QuadtreeNodeComponent* TryAndGetQuadtreeNodeMutable(QuadtreeInstance* instance, const ecs::Entity entity);
    static QuadtreeNodeComponent& GetQuadtreeNodeRefMutable(QuadtreeInstance* instance, const ecs::Entity entity);
    static const QuadtreeNodeComponent* TryAndGetQuadtreeNode(QuadtreeInstance* instance, const ecs::Entity entity);
    static const QuadtreeNodeComponent& GetQuadtreeNodeRef(QuadtreeInstance* instance, const ecs::Entity entity);

private:

};

class TerrainQuadtree {
    TerrainQuadtree(const TerrainQuadtree&) = delete;
    TerrainQuadtree& operator=(const TerrainQuadtree&) = delete;
public:

    TerrainQuadtree(const float& split_factor, const size_t& max_detail_level, const double& root_side_length, const glm::vec3& root_tile_position);
    void UpdateQuadtree(const glm::vec3& camera_position, const glm::mat4& view, const glm::mat4& projection);

private:

    std::unique_ptr<TerrainNode> root;
    std::unordered_map<glm::ivec3, std::unique_ptr<HeightNode>> cachedHeightData;
    size_t MaxLOD;

};

#endif // !TERRAIN_PLUGIN_QUADTREE_HPP
