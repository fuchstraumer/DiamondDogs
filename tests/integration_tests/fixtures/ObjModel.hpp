#pragma once
#ifndef RESOURCE_CONTEXT_OBJ_MODEL_HPP
#define RESOURCE_CONTEXT_OBJ_MODEL_HPP
#include "ResourceTypes.hpp"
#include "../VolumetricForward/vtfStructs.hpp"
#include "../VolumetricForward/vtfFrameData.hpp"
#pragma warning(push, 1)
#include "tinyobjloader/experimental/tinyobj_loader_opt.h"
#pragma warning(pop)
#include <vector>
#include <set>
#include <map>
#include "Material.hpp"

class DescriptorBinder;

class ObjectModel
{
public:

    ObjectModel();
    void LoadModelFromFile(const char* model_filename, const char* material_directory);
    void Render(const objRenderStateData& state);

private:

    enum BindMode
    {
        None = 0u,
        VBO0Only = 1u, // unindexed primitives
        VBO0AndEBO = 2u, // position only indexed primitives
        VBO1Only = 3u, // attributes
        VBO1AndEBO = 4u, // attributes and indices
        All // positions, shading attributes, and indices
    };

    void Bind(VkCommandBuffer cmd, BindMode mode);
    // call to initialize VBOs and EBO
    void CreateBuffers();

    void loadMeshes(const std::vector<tinyobj_opt::shape_t>& shapes, const tinyobj_opt::attrib_t& attrib);

    void renderGeometry(const objRenderStateData& state);
    void renderIdx(VkCommandBuffer cmd, const size_t index, DescriptorBinder* binder);

    struct Part
    {
        uint32_t startIdx;
        uint32_t idxCount;
        int32_t vertexOffset;
        uint32_t mtlIdx;
        AABB aabb;
        constexpr bool operator==(const Part& other) const noexcept;
        constexpr bool operator<(const Part& other) const noexcept;
    };

    std::set<Part> parts;

    struct vtxAttrib
    {
        glm::vec3 normal;
        glm::vec3 tangent;
        glm::vec2 uv;
    };

    std::vector<glm::vec3> positions;
    std::vector<vtxAttrib> attributes;
    // we first buffer everything in here, then later
    // we copy into indicesUi16 and clear this if we can
    std::vector<uint32_t> indicesUi32;
    std::vector<uint16_t> indicesUi16;
    bool indicesAreUi16;
    uint32_t vtxCount{ 0u };
    uint32_t idxCount{ 0u };
    VulkanResource* VBO0{ nullptr };
    VulkanResource* VBO1{ nullptr };
    VulkanResource* EBO{ nullptr };
    std::string modelName;

    std::vector<Material> materials;

    void createMaterials(const std::vector<tinyobj_opt::material_t>& mtls, const char* search_dir);
    void generateIndirectDraws();

    std::multimap<size_t, VkDrawIndexedIndirectCommand> indirectCommands;
    uint32_t cmdOffset{ 0u };
    size_t numMaterials{ 0u };
    std::vector<std::string> PartNames;
    std::map<size_t, VulkanResource*> materialUBOs;

    AABB modelAABB;
    VulkanResource* IndirectDrawBuffers{ nullptr };
    const bool multiDrawIndirect;

};

#endif //!RESOURCE_CONTEXT_OBJ_MODEL_HPP
