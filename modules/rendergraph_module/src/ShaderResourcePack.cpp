#include "graph/ShaderResourcePack.hpp"
#include "resource/DescriptorPool.hpp"
#include "resource/DescriptorSet.hpp"
#include "resource/DescriptorSetLayout.hpp"
#include "core/ShaderPack.hpp"
#include "graph/RenderGraph.hpp"
#include "systems/BufferResourceCache.hpp"
#include "systems/ImageResourceCache.hpp"
#include "RenderingContext.hpp"
#include "core/ShaderResource.hpp"
#include "core/ResourceUsage.hpp"
#include "core/ResourceGroup.hpp"
#include "core/Shader.hpp"
#include "resource/Buffer.hpp"

namespace vpsk {

    ShaderResourcePack::ShaderResourcePack(RenderGraph& _graph, const st::ShaderPack * pack) : shaderPack(pack), graph(_graph) {
        getGroupNames();
        createDescriptorPool();
        createSets();
    }

    ShaderResourcePack::~ShaderResourcePack() {}

    vpr::DescriptorSet* ShaderResourcePack::DescriptorSet(const char* group_name) noexcept {
        auto iter = rsrcGroupToIdxMap.find(group_name);
        if (iter != rsrcGroupToIdxMap.end()) {
            return descriptorSets[rsrcGroupToIdxMap.at(group_name)].get();
        }
        else {
            return nullptr;
        }
    }

    const vpr::DescriptorSet* ShaderResourcePack::DescriptorSet(const char* group_name) const noexcept {
        auto iter = rsrcGroupToIdxMap.find(group_name);
        if (iter != rsrcGroupToIdxMap.cend()) {
            return descriptorSets[rsrcGroupToIdxMap.at(group_name)].get();
        }
        else {
            return nullptr;
        }
    }

    vpr::DescriptorPool* ShaderResourcePack::DescriptorPool() noexcept {
        return descriptorPool.get();
    }

    const vpr::DescriptorPool* ShaderResourcePack::DescriptorPool() const noexcept {
        return descriptorPool.get();
    }

    void ShaderResourcePack::getGroupNames() {
        auto names = shaderPack->GetResourceGroupNames();
        for (size_t i = 0; i < names.NumStrings; ++i) {
            rsrcGroupToIdxMap.emplace(names.Strings[i], i);
        }
    }

    void ShaderResourcePack::createDescriptorPool() {
        auto& renderer = RenderingContext::GetRenderer();
        descriptorPool = std::make_unique<vpr::DescriptorPool>(renderer.Device(), rsrcGroupToIdxMap.size());
        const auto& rsrc_counts = shaderPack->GetTotalDescriptorTypeCounts();
        descriptorPool->AddResourceType(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, rsrc_counts.UniformBuffers);
        descriptorPool->AddResourceType(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, rsrc_counts.UniformBuffersDynamic);
        descriptorPool->AddResourceType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, rsrc_counts.StorageBuffers);
        descriptorPool->AddResourceType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, rsrc_counts.StorageBuffersDynamic);
        descriptorPool->AddResourceType(VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, rsrc_counts.StorageTexelBuffers);
        descriptorPool->AddResourceType(VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, rsrc_counts.UniformTexelBuffers);
        descriptorPool->AddResourceType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, rsrc_counts.StorageImages);
        descriptorPool->AddResourceType(VK_DESCRIPTOR_TYPE_SAMPLER, rsrc_counts.Samplers);
        descriptorPool->AddResourceType(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, rsrc_counts.SampledImages);
        descriptorPool->AddResourceType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, rsrc_counts.CombinedImageSamplers);
        descriptorPool->Create();
    }

    constexpr static bool is_buffer_type(const VkDescriptorType type) {
        if (type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER || type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC ||
            type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER || type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC) {
            return true;
        }
        else {
            return false;
        }
    }

    constexpr static bool is_texel_buffer(const VkDescriptorType type) {
        if (type == VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER || type == VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER) {
            return true;
        }
        else {
            return false;
        }
    }

    void ShaderResourcePack::createResources(const std::vector<const st::ShaderResource*>& resources) {
        std::vector<const st::ShaderResource*> buffer_resources;
        std::vector<const st::ShaderResource*> image_resources;
        for (const auto& rsrc : resources) {
            if (is_buffer_type(rsrc->DescriptorType()) || is_texel_buffer(rsrc->DescriptorType())) {
                buffer_resources.emplace_back(rsrc);
            }
            else {
                image_resources.emplace_back(rsrc);
            }
        }

        auto& buffer_cache = graph.GetBufferResourceCache();
        buffer_cache.AddResources(buffer_resources);
        auto& image_cache = graph.GetImageResourceCache();
        image_cache.AddResources(image_resources);
    }

    void ShaderResourcePack::createSingleSet(const std::string& name) {
        size_t num_resources = 0;
        const st::ResourceGroup* resource_group = shaderPack->GetResourceGroup(name.c_str());
        resource_group->GetResourcePtrs(&num_resources, nullptr);
        std::vector<const st::ShaderResource*> resources(num_resources);
        resource_group->GetResourcePtrs(&num_resources, resources.data());
        createResources(resources);
    }

    void ShaderResourcePack::createSets() {
        for (const auto& entry : rsrcGroupToIdxMap) {
            createSingleSet(entry.first);
        }
    }

}
