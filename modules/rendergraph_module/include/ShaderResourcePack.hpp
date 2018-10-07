#pragma once
#ifndef DIAMOND_DOGS_SHADER_RESOURCE_PACK_HPP
#define DIAMOND_DOGS_SHADER_RESOURCE_PACK_HPP
#include "ForwardDecl.hpp"
#include <unordered_map>
#include <vector>
#include <memory>
#include <set>
#include <vulkan/vulkan.h>

namespace st {
    class ShaderPack;
    class ShaderResource;
    class ResourceUsage;
    class Shader;
}

class RenderGraph;
struct VulkanResource;
class GeneratedPipeline;

/*
    Creates descriptor sets, layouts, and a singular descriptor pool
    required to use a shaderpack.
    
*/
class ShaderResourcePack {
    ShaderResourcePack(const ShaderResourcePack&) = delete;
    ShaderResourcePack& operator=(const ShaderResourcePack&) = delete;
    friend class RenderGraph;
    friend class GeneratedPipeline;

public:

    // ShaderPacks aren't owned/loaded by this object: they are cached/stored elsewhere
    ShaderResourcePack(RenderGraph* _graph, const st::ShaderPack* pack);
    ShaderResourcePack(ShaderResourcePack&& other) noexcept = default;
    ShaderResourcePack& operator=(ShaderResourcePack&& other) noexcept = default;
    ~ShaderResourcePack();

    vpr::DescriptorSet* DescriptorSet(const char* rsrc_group_name) noexcept;
    const vpr::DescriptorSet* DescriptorSet(const char* rsrc_group_name) const noexcept;
    vpr::DescriptorPool* DescriptorPool() noexcept;
    const vpr::DescriptorPool* DescriptorPool() const noexcept;
    std::vector<VkDescriptorSet> ShaderGroupSets(const std::string& name) const noexcept;
    void BindGroupSets(VkCommandBuffer cmd, const std::string& shader_group_name, const VkPipelineBindPoint bind_point) const;

    VulkanResource* At(const std::string& group_name, const std::string& name);
    VulkanResource* Find(const std::string& group_name, const std::string& name) noexcept;
    bool Has(const std::string& group_name, const std::string& name) const noexcept;

private:

    void createDescriptorPool();
    void createSetLayouts();
    void createSets();
    void createSingleSet(const std::string& name);
    void getGroupNames();
    void parseGroupBindingInfo();

    void createResources(const std::vector<const st::ShaderResource*>& resources);
    void createResource(const st::ShaderResource * rsrc);
    void createSampledImage(const st::ShaderResource* rsrc);
    void createTexelBuffer(const st::ShaderResource * texel_buffer, bool storage);
    void createUniformBuffer(const st::ShaderResource * uniform_buffer);
    void createStorageBuffer(const st::ShaderResource * storage_buffer);
    void createCombinedImageSampler(const st::ShaderResource* rsrc);
    void createSampler(const st::ShaderResource* rsrc);

    RenderGraph* graph;
    std::unique_ptr<vpr::DescriptorPool> descriptorPool;
    std::unordered_map<std::string, size_t> rsrcGroupToIdxMap;
    std::vector<std::unique_ptr<vpr::DescriptorSet>> descriptorSets;
    std::vector<std::unique_ptr<vpr::PipelineLayout>> pipelineLayouts;
    std::unordered_map<std::string, std::unordered_map<std::string, VulkanResource*>> resources;
    // array stores indices into descriptorSets used by each group
    std::unordered_map<std::string, size_t> shaderGroupNameIdxMap;
    std::unordered_map<size_t, std::vector<std::unique_ptr<vpr::DescriptorSetLayout>>> shaderSetLayouts;
    std::vector<std::set<size_t>> groupResourceUsages;
    std::vector<const st::Shader*> shaderGroups;
    const st::ShaderPack* shaderPack;
};

#endif //!DIAMOND_DOGS_SHADER_RESOURCE_PACK_HPP
