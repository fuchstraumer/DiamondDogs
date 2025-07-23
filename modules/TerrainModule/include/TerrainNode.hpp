#pragma once
#ifndef TERRAIN_PLUGIN_TERRAIN_NODE_HPP
#define TERRAIN_PLUGIN_TERRAIN_NODE_HPP
#include "ForwardDecl.hpp"
#include "PlanarMesh.hpp"
#include "HeightNode.hpp"
#include "AABB.hpp"
#include "Entity.hpp"
#include <array>
#include <future>

class TerrainQuadtree;
class NodeRenderer;
struct ViewFrustum;

struct TerrainConfiguration
{
    float MaxRenderDistance{ 3000.0f };
    float SwitchRatio{ 1.80f };
    size_t MaxLOD{ 16u };
    static const TerrainConfiguration& Get() noexcept;
    static TerrainConfiguration& GetMutable() noexcept;
};

struct QuadtreeNodeComponent
{
    std::array<ecs::Entity, 4> Children;
    // xyz are for conventional 3D gridding, as needed. w is used 
    // for things like the cubemap faces of a bigger world, or regional layers
    unsigned int GridX : 16;
    unsigned int GridY : 16;
    uint32_t lodLevel;
    ecs::Entity ParentHandle{ ecs::NULL_ENTITY };
    // Data payload: set to a user defined type, most likely stored elsewhere
};

struct TerrainNodeSystem
{
    TerrainNodeSystem(TerrainQuadtree* ptr, size_t maxLOD, float switchRatio);

private:
    TerrainQuadtree* quadtree;
    std::vector<TerrainNodeComponent> components;
};

class TerrainNode {
    TerrainNode(const TerrainNode& other) = delete;
    TerrainNode& operator=(const TerrainNode& other) = delete;
public:

    TerrainNode(const glm::ivec3& parent_coords, const glm::ivec3& logical_coords, const glm::vec3& position, const double& length);
    ~TerrainNode();

    void Subdivide();
    // updates this nodes status, and then the node adds itself to the renderer's pool of nodes if its going to be rendered.
    void Update(const glm::vec3& camera_position, const ViewFrustum& view);
    // true if all of the Child pointers are nullptr
    bool IsLeaf() const;
    // Recursive method to clean up node tree
    void Prune();
    int LOD_Level() const;
    void CreateMesh(const vpr::Device* dvc);
    void CreateHeightData(const HeightNode* parent_node);
    
    static size_t MaxLOD;
    static float SwitchRatio;

    std::array<std::shared_ptr<TerrainNode>, 4> Children;
    std::unique_ptr<HeightNode> HeightData;
    // used w/ node renderer to decide if this is being rendered and/or if it needs data transferred
    // to the device (so we can batch transfer commands)
    NodeStatus Status;
    PlanarMesh mesh;
    // Coordinates of this node in the grid defining the quadtree
    // xy are the grid positions, z is the LOD level.
    glm::ivec3 GridCoordinates;
    glm::ivec3 ParentGridCoordinates;
    // world-relative spatial coordinates.
    glm::vec3 SpatialCoordinates;
    // Length of one side of the node: should be equivalent to (1 / depth) * L, where L is the
    // length of the root nodes side.
    float SideLength;
    AABB aabb;

private:
    std::future<std::unique_ptr<HeightNode>> heightDataFuture;
};

#endif // !TERRAIN_PLUGIN_TERRAIN_NODE_HPP
