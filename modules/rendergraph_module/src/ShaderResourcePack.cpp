#include "ShaderResourcePack.hpp"
#include "DescriptorPool.hpp"
#include "DescriptorSet.hpp"
#include "DescriptorSetLayout.hpp"
#include "core/ShaderPack.hpp"
#include "RenderGraph.hpp"
#include "RenderingContext.hpp"
#include "ResourceContext.hpp"
#include "LogicalDevice.hpp"
#include "CreateInfoBase.hpp"
#include "core/ShaderResource.hpp"
#include "core/ResourceUsage.hpp"
#include "core/ResourceGroup.hpp"
#include "core/Shader.hpp"

ShaderResourcePack::ShaderResourcePack(RenderGraph* _graph, const st::ShaderPack * pack) : shaderPack(pack), graph(_graph) {
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

const VulkanResource* ShaderResourcePack::At(const std::string& group_name, const std::string& name) const {
    return resources.at(group_name).at(name);
}

bool ShaderResourcePack::Has(const std::string& group_name, const std::string& name) const noexcept {
    if (resources.count(group_name) == 0) {
        return false;
    }
    else {
        if (resources.at(group_name).count(name) == 0) {
            return false;
        }
        else {
            return true;
        }
    }
}

const VulkanResource* ShaderResourcePack::Find(const std::string& group_name, const std::string& name) const noexcept {
    auto group_iter = resources.find(group_name);
    if (group_iter != std::cend(resources)) {
        auto rsrc_iter = group_iter->second.find(name);
        if (rsrc_iter != std::cend(group_iter->second)) {
            return rsrc_iter->second;
        }
        else {
            return nullptr;
        }
    }
    else {
        return nullptr;
    }
}

void ShaderResourcePack::createDescriptorPool() {
    auto& renderer = RenderingContext::Get();
    descriptorPool = std::make_unique<vpr::DescriptorPool>(renderer.Device()->vkHandle(), rsrcGroupToIdxMap.size());
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

void ShaderResourcePack::createSets() {
    for (const auto& entry : rsrcGroupToIdxMap) {
        createSingleSet(entry.first);
    }
}

void ShaderResourcePack::createSingleSet(const std::string& name) {
    size_t num_resources = 0;
    const st::ResourceGroup* resource_group = shaderPack->GetResourceGroup(name.c_str());
    {
        auto tags = resource_group->GetTags();
        std::vector<std::string> tag_strings;
        for (size_t i = 0; i < tags.NumStrings; ++i) {
            tag_strings.emplace_back(tags[i]);
        }
        
        if (auto iter = std::find(std::begin(tag_strings), std::end(tag_strings), "MaterialGroup"); iter != std::end(tag_strings)) {
            // If we find a material group, don't create meta-information for it.
            return;
        }
    }
    resource_group->GetResourcePtrs(&num_resources, nullptr);
    std::vector<const st::ShaderResource*> resources(num_resources);
    resource_group->GetResourcePtrs(&num_resources, resources.data());
    createResources(resources);
}

void ShaderResourcePack::getGroupNames() {
    auto names = shaderPack->GetResourceGroupNames();
    for (size_t i = 0; i < names.NumStrings; ++i) {
        rsrcGroupToIdxMap.emplace(names.Strings[i], i);
    }
}

void ShaderResourcePack::createResources(const std::vector<const st::ShaderResource*>& resources) {
    for (const auto& rsrc : resources) {
        if (!Has(rsrc->ParentGroupName(), rsrc->Name())) {
            createResource(rsrc);
        }
    }
}

void ShaderResourcePack::createResource(const st::ShaderResource* rsrc) {
    switch (rsrc->DescriptorType()) {
    case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
        createTexelBuffer(rsrc, false);
        break;
    case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
        createTexelBuffer(rsrc, true);
        break;
    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
        createUniformBuffer(rsrc);
        break;
    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
        createStorageBuffer(rsrc);
        break;
    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
        createUniformBuffer(rsrc);
        break;
    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
        createStorageBuffer(rsrc);
        break;
    case VK_DESCRIPTOR_TYPE_SAMPLER:
        createSampler(rsrc);
        break;
    case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
        createSampledImage(rsrc);
        break;
    case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
        createCombinedImageSampler(rsrc);
        break;
    case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
        createSampledImage(rsrc);
        break;
    case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
        createCombinedImageSampler(rsrc);
        break;
    default:
        throw std::domain_error("ShaderResource had invalid type!");
    }
}

void ShaderResourcePack::createSampledImage(const st::ShaderResource* rsrc) {
    if (rsrc->FromFile()) {
        return;
    }

    auto& rsrc_context = ResourceContext::Get();
    const VkImageCreateInfo* image_info = &rsrc->ImageInfo();
    const VkImageViewCreateInfo* view_info = &rsrc->ImageViewInfo();

    auto emplaced = resources[rsrc->ParentGroupName()].emplace(rsrc->Name(), rsrc_context.CreateImage(image_info, view_info, 0, nullptr, memory_type::DEVICE_LOCAL, nullptr));
    if (!emplaced.second) {
        throw std::runtime_error("Failed to create sampler resource");
    }

}

void ShaderResourcePack::createTexelBuffer(const st::ShaderResource* texel_buffer, bool storage) {
    auto& group = resources[texel_buffer->ParentGroupName()];
    VkBufferCreateInfo buffer_info = vpr::vk_buffer_create_info_base;
    buffer_info.usage = storage ? VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT : VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT;
    buffer_info.size = texel_buffer->MemoryRequired();
    const VkBufferViewCreateInfo* view_info = &texel_buffer->BufferViewInfo();
    auto& rsrc_context = ResourceContext::Get();
    group.emplace(texel_buffer->Name(), rsrc_context.CreateBuffer(&buffer_info, view_info, 0, nullptr, memory_type::DEVICE_LOCAL, nullptr));
}

void ShaderResourcePack::createUniformBuffer(const st::ShaderResource* uniform_buffer) {
    auto& group = resources[uniform_buffer->ParentGroupName()];
    VkBufferCreateInfo buffer_info = vpr::vk_buffer_create_info_base;
    buffer_info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    buffer_info.size = uniform_buffer->MemoryRequired();
    auto& rsrc_context = ResourceContext::Get();
    group.emplace(uniform_buffer->Name(), rsrc_context.CreateBuffer(&buffer_info, nullptr, 0, nullptr, memory_type::HOST_VISIBLE_AND_COHERENT, nullptr));
}

void ShaderResourcePack::createStorageBuffer(const st::ShaderResource* storage_buffer) {
    auto& group = resources[storage_buffer->ParentGroupName()];
    VkBufferCreateInfo buffer_info = vpr::vk_buffer_create_info_base;
    buffer_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    buffer_info.size = storage_buffer->MemoryRequired();
    auto& rsrc_context = ResourceContext::Get();
    group.emplace(storage_buffer->Name(), rsrc_context.CreateBuffer(&buffer_info, nullptr, 0, nullptr, memory_type::DEVICE_LOCAL, nullptr));
}

void ShaderResourcePack::createCombinedImageSampler(const st::ShaderResource* rsrc) {
    createSampler(rsrc);
    createSampledImage(rsrc);
}

void ShaderResourcePack::createSampler(const st::ShaderResource* rsrc) {
    const char* group_name = rsrc->ParentGroupName();
    const char* rsrc_name = rsrc->Name();
    auto& rsrc_context = ResourceContext::Get();
    auto emplaced = resources[group_name].emplace(rsrc_name, rsrc_context.CreateSampler(&rsrc->SamplerInfo(), nullptr));
    if (!emplaced.second) {
        throw std::runtime_error("Failed to create sampler resource.");
    }
}
