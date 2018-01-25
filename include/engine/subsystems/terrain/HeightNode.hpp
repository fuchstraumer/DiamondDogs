#pragma once
#ifndef VULPES_TERRAIN_HEIGHT_NODE_H
#define VULPES_TERRAIN_HEIGHT_NODE_H
#include "stdafx.h"
#include "engine\util\noise.hpp"

namespace terrain {

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

    void SetSamples(std::vector<HeightSample>&& samples);

    float GetHeight(const glm::vec2 world_pos) const;

    float Sample(const size_t& x, const size_t& y) const;
    float Sample(const size_t& idx) const;

    const HeightSample& operator[](const size_t& idx) const;
    HeightSample& operator[](const size_t& idx);

    const glm::ivec3& GridCoords() const noexcept;

    size_t MeshGridSize() const noexcept;
    size_t SampleGridSize() const noexcept;

    size_t NumSamples() const noexcept;
    std::vector<glm::vec2> GetHeights() const;
    std::vector<HeightSample> GetSamples() const;

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

static inline std::unique_ptr<HeightNode> CreateHeightNodeFromParent(const glm::ivec3& grid_coords, const HeightNode* parent_node) {
    return std::move(std::make_unique<HeightNode>(grid_coords, parent_node));
}

}

#endif // !VULPES_HEIGHTMAP_H
