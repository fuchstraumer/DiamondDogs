#include "ShaderResourceCache.hpp"
#include "ResourceTypes.hpp"
#include "ResourceContext.hpp"
#include "core/ShaderResource.hpp"
#include "CreateInfoBase.hpp"

ShaderResourceCache::ShaderResourceCache(std::string shader_name) : shaderName(shader_name) {}

ShaderResourceCache::~ShaderResourceCache() {}

const std::string & ShaderResourceCache::Name() const noexcept {
    return shaderName;
}

void ShaderResourceCache::AddResources(const std::vector<const st::ShaderResource*>& resources) {
    for (const auto& rsrc : resources) {
        AddResource(rsrc);
    }
}

void ShaderResourceCache::AddResource(const st::ShaderResource* resource) {
    if (!Has(resource->ParentGroupName(), resource->Name())) {
        createResource(resource);
    }
}

const VulkanResource* ShaderResourceCache::At(const std::string& group_name, const std::string& name) const {
    return nullptr;
}

const VulkanResource* ShaderResourceCache::Find(const std::string& group_name, const std::string& name) const noexcept {
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

bool ShaderResourceCache::Has(const std::string& group_name, const std::string& name) const noexcept {
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

void ShaderResourceCache::createResources(const std::vector<const st::ShaderResource*>& resources) {
}

void ShaderResourceCache::createResource(const st::ShaderResource* rsrc) {
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
        // Probably just a resource type we don't care about.
        break;
    }
}

void ShaderResourceCache::createSampledImage(const st::ShaderResource* rsrc) {
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

void ShaderResourceCache::createTexelBuffer(const st::ShaderResource* texel_buffer, bool storage) {
    auto& group = resources[texel_buffer->ParentGroupName()];
    VkBufferCreateInfo buffer_info = vpr::vk_buffer_create_info_base;
    buffer_info.flags = storage ? VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT : VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT;
    buffer_info.size = texel_buffer->MemoryRequired();
    const VkBufferViewCreateInfo* view_info = &texel_buffer->BufferViewInfo();
    auto& rsrc_context = ResourceContext::Get();
    group.emplace(texel_buffer->Name(), rsrc_context.CreateBuffer(&buffer_info, view_info, 0, nullptr, memory_type::DEVICE_LOCAL, nullptr));
}

void ShaderResourceCache::createUniformBuffer(const st::ShaderResource* uniform_buffer) {
    auto& group = resources[uniform_buffer->ParentGroupName()];
    VkBufferCreateInfo buffer_info = vpr::vk_buffer_create_info_base;
    buffer_info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    buffer_info.size = uniform_buffer->MemoryRequired();
    auto& rsrc_context = ResourceContext::Get();
    group.emplace(uniform_buffer->Name(), rsrc_context.CreateBuffer(&buffer_info, nullptr, 0, nullptr, memory_type::HOST_VISIBLE_AND_COHERENT, nullptr));
}

void ShaderResourceCache::createStorageBuffer(const st::ShaderResource* storage_buffer) {
    auto& group = resources[storage_buffer->ParentGroupName()];
    VkBufferCreateInfo buffer_info = vpr::vk_buffer_create_info_base;
    buffer_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    buffer_info.size = storage_buffer->MemoryRequired();
    auto& rsrc_context = ResourceContext::Get();
    group.emplace(storage_buffer->Name(), rsrc_context.CreateBuffer(&buffer_info, nullptr, 0, nullptr, memory_type::DEVICE_LOCAL, nullptr));
}

void ShaderResourceCache::createCombinedImageSampler(const st::ShaderResource* rsrc) {
    createSampler(rsrc);
    createSampledImage(rsrc);
}

void ShaderResourceCache::createSampler(const st::ShaderResource* rsrc) {
    const char* group_name = rsrc->ParentGroupName();
    const char* rsrc_name = rsrc->Name();
    auto& rsrc_context = ResourceContext::Get();
    auto emplaced = resources[group_name].emplace(rsrc_name, rsrc_context.CreateSampler(&rsrc->SamplerInfo(), nullptr));
    if (!emplaced.second) {
        throw std::runtime_error("Failed to create sampler resource.");
    }
}
