#pragma once
#ifndef TERRAIN_PLUGIN_QUADTREE_HPP
#define TERRAIN_PLUGIN_QUADTREE_HPP
#include "ForwardDecl.hpp"
#include "glm/vec3.hpp"
#include "glm/mat4x4.hpp"
#include <unordered_map>
#include <memory>
#include <vulkan/vulkan.h>

class HeightNode;
class TerrainNode;

class TerrainQuadtree {
    TerrainQuadtree(const TerrainQuadtree&) = delete;
    TerrainQuadtree& operator=(const TerrainQuadtree&) = delete;
public:

    TerrainQuadtree(const vpr::Device* device, const float& split_factor, const size_t& max_detail_level, const double& root_side_length, const glm::vec3& root_tile_position);

    void SetupNodePipeline(const VkRenderPass& renderpass, const glm::mat4& projection);
    void UpdateQuadtree(const glm::vec3 & camera_position, const glm::mat4& view);
    void RenderNodes(VkCommandBuffer& graphics_cmd, VkCommandBufferBeginInfo& begin_info, const glm::mat4& view, const glm::vec3& camera_pos, const VkViewport& viewport, const VkRect2D& rect);

private:

    std::unique_ptr<TerrainNode> root;
    std::unordered_map<glm::ivec3, std::shared_ptr<HeightNode>> cachedHeightData;
    size_t MaxLOD;

};

#endif // !TERRAIN_PLUGIN_QUADTREE_HPP
