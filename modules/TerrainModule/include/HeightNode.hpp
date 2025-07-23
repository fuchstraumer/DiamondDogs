#pragma once
#ifndef TERRAIN_PLUGIN_HEIGHT_NODE_HPP
#define TERRAIN_PLUGIN_HEIGHT_NODE_HPP
#include "HeightSample.hpp"
#include "glm/vec3.hpp"
#include <vector>
#include <memory>

enum class NodeStatus {
    Undefined, // Initial value for all nodes. If a node has this, it has not been properly constructed (or has been deleted)
    Active, // Being used in next frame
    Subdivided, // Has been subdivided, render children instead of this.
    NeedsUnload, // Erase and unload resources.
    NeedsTransfer,
    RequestData, // Request height data from the upsampling DataProducer
};

class HeightNode {
public:

    // Used for root node 
    HeightNode(const glm::ivec3& node_grid_coordinates, std::vector<HeightSample> init_samples);
    // Used for most nodes.
    HeightNode(const glm::ivec3& node_grid_coordinates, const HeightNode* parent);

    ~HeightNode() = default;
    
    void SampleFromParent();
    void SetSamples(std::vector<HeightSample> samples);
    float GetHeight(const glm::vec2 world_pos) const;

    float Sample(const size_t& x, const size_t& y) const;
    float Sample(const size_t& idx) const;
    const HeightSample& operator[](const size_t& idx) const;
    HeightSample& operator[](const size_t& idx);

    const glm::ivec3& GridCoords() const noexcept;
    size_t MeshGridSize() const noexcept;
    size_t SampleGridSize() const noexcept;
    size_t NumSamples() const noexcept;
    const std::vector<HeightSample>& GetSamples() const;

    static void SetRootNodeSize(const size_t& new_size);
    static void SetRootNodeLength(const double& new_length);
    static size_t RootSampleGridSize;
    static double RootNodeLength;   
    float MinZ, MaxZ;
    const HeightNode* Parent;

protected:
    size_t sampleGridSize = 16;
    size_t meshGridSize;
    std::vector<HeightSample> samples;
    glm::ivec3 gridCoords;
    size_t nodeID;

};

std::unique_ptr<HeightNode> CreateHeightNodeFromParent(const glm::ivec3& grid_coords, const HeightNode* parent_node);

#endif // !TERRAIN_PLUGIN_HEIGHT_NODE_HPP
