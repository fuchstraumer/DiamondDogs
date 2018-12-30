#pragma once
#ifndef DIAMOND_DOGS_SHADER_RESOURCE_PACK_HPP
#define DIAMOND_DOGS_SHADER_RESOURCE_PACK_HPP
#include "ForwardDecl.hpp"
#include <unordered_map>
#include <list>
#include <vector>
#include <memory>
#include <set>
#include <vulkan/vulkan.h>

namespace st {
    class ShaderPack;
    class ShaderResource;
    class ResourceUsage;
    class Shader;
    class ResourceGroup;
}

class Descriptor;
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

    Descriptor* GetDescriptor(const std::string& name);
    VkPipelineLayout PipelineLayout(const std::string& name) const;
    vpr::DescriptorPool* DescriptorPool() noexcept;
    const vpr::DescriptorPool* DescriptorPool() const noexcept;
    std::vector<VkDescriptorSet> ShaderGroupSets(const std::string& name) const noexcept;
    void BindGroupSets(VkCommandBuffer cmd, const std::string& shader_group_name, const VkPipelineBindPoint bind_point) const;

    VulkanResource* At(const std::string& group_name, const std::string& name);
    VulkanResource* Find(const std::string& group_name, const std::string& name) noexcept;
    size_t BindingLocation(const std::string& name) const noexcept;
    bool Has(const std::string& group_name, const std::string& name) const noexcept;

private:

    void createDescriptorPool();
    void createSets();
    void createSetResourcesAndLayout(const std::string& name);
    void createDescriptor(const std::string& name, const std::vector<const st::ShaderResource*> resources, const bool skip_physical_resources);
    void getGroupNames();
    void parseGroupBindingInfo();
    void createPipelineLayout(const std::string& name);

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
    std::vector<const st::ResourceGroup*> resourceGroups;
    std::unordered_map<std::string, size_t> rsrcGroupToIdxMap;
    std::vector<std::unique_ptr<Descriptor>> descriptorSets;
    std::vector<std::unique_ptr<vpr::PipelineLayout>> pipelineLayouts;
    std::unordered_map<std::string, std::unordered_map<std::string, VulkanResource*>> resources;
    std::unordered_map<const VulkanResource*, VkDescriptorType> resourceTypesMap;
    std::unordered_map<std::string, size_t> resourceBindingLocations;
    std::unordered_map<std::string, uint32_t> shaderGroupNameIdxMap;
    std::vector<std::vector<size_t>> shaderGroupResourceGroupUsages;
    std::vector<const st::Shader*> shaderGroups;
    const st::ShaderPack* shaderPack;
};

#endif //!DIAMOND_DOGS_SHADER_RESOURCE_PACK_HPP
