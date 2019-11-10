#pragma once
#ifndef DIAMOND_DOGS_DESCRIPTOR_PACK_HPP
#define DIAMOND_DOGS_DESCRIPTOR_PACK_HPP
#include "ForwardDecl.hpp"
#include <unordered_map>
#include <vector>
#include <memory>
#include <vulkan/vulkan.h>
#include "DescriptorBinder.hpp"

namespace st {
    class ShaderPack;
    class ShaderResource;
    class ResourceUsage;
    class Shader;
    class ResourceGroup;
}

class DescriptorTemplate;
class Descriptor;
struct VulkanResource;

/*
    Creates the majority of the descriptor-related objects we will need.
*/
class DescriptorPack {
    DescriptorPack(const DescriptorPack&) = delete;
    DescriptorPack& operator=(const DescriptorPack&) = delete;
    friend class RenderGraph;
    friend class GeneratedPipeline;

public:

    // ShaderPacks aren't owned/loaded by this object: they are cached/stored elsewhere
    DescriptorPack(const st::ShaderPack* pack);
    DescriptorPack(DescriptorPack&& other) noexcept = default;
    DescriptorPack& operator=(DescriptorPack&& other) noexcept = default;
    ~DescriptorPack();

    void Reset();

    VkPipelineLayout PipelineLayout(const std::string& name) const;
    // retrieve this to update bindings for this descriptor
    Descriptor* RetrieveDescriptor(const std::string& rsrc_group_name);
    // the binder will always have the most-recently updated bindings from the descriptors it uses,
    // but may be more expensive to create the first time. prefer copying between binders of the same
    // group over initializing like this
    DescriptorBinder RetrieveBinder(const std::string& shader_group);

    // internally swaps descriptors between the two double-buffered sets.
    // they cross-comunicate and do a clone the first time, so the heuristics combine
    void EndFrame();

private:

    void retrieveResourceGroups();
    void createDescriptorTemplates();
    void parseGroupBindingInfo();
    void createDescriptors();
    void createPipelineLayout(const std::string& name);
    std::unique_ptr<Descriptor> createDescriptor(const std::string& rsrc_group_name, std::unordered_map<std::string, size_t>&& binding_locs);
    friend class Descriptor;
    std::vector<const st::ResourceGroup*> resourceGroups;
    std::unordered_map<std::string, size_t> rsrcGroupToIdxMap;
    std::vector<std::unique_ptr<DescriptorTemplate>> descriptorTemplates;
    std::vector<std::unique_ptr<Descriptor>> descriptors;
    std::vector<std::unique_ptr<Descriptor>> lastFrameDescriptors;
    std::vector<std::unique_ptr<vpr::PipelineLayout>> pipelineLayouts;
    std::unordered_map<std::string, uint32_t> shaderGroupNameIdxMap;
    std::unordered_map<std::string, std::unordered_map<std::string, size_t>> bindingLocations;
    std::vector<size_t> rsrcGroupUseFrequency;
    std::vector<std::vector<size_t>> shaderGroupResourceGroupUsages;
    std::vector<const st::Shader*> shaderGroups;
    const st::ShaderPack* shaderPack;
    size_t frameIdx{ 0u };
};

#endif //!DIAMOND_DOGS_DESCRIPTOR_PACK_HPP
