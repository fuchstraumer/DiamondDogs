#include "systems/BufferResourceCache.hpp"
#include "ResourceTypes.hpp"
#include "ResourceContext.hpp"
#include "core/ShaderResource.hpp"


BufferResourceCache::BufferResourceCache(const vpr::Device* dvc) : device(dvc) { }

BufferResourceCache::~BufferResourceCache() {}

void BufferResourceCache::AddResources(const std::vector<const st::ShaderResource*>& resources) {
    createResources(resources);
}

void BufferResourceCache::AddResource(const st::ShaderResource* resource) {
    if (!HasResource(resource->ParentGroupName(), resource->Name())) {
        createResource(resource);
    }
}

VulkanResource* BufferResourceCache::at(const std::string& group, const std::string& name) {
    return buffers.at(group).at(name);
}

VulkanResource* BufferResourceCache::find(const std::string& group, const std::string& name) noexcept {
    auto group_iter = buffers.find(group);
    if (group_iter != buffers.end()) {
        auto rsrc_iter = group_iter->second.find(name);
        if (rsrc_iter != group_iter->second.end()) {
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

bool BufferResourceCache::HasResource(const std::string& group, const std::string& name) const noexcept {
    if (buffers.count(group) == 0) {
        return false;
    }
    else if (buffers.at(group).count(name) != 0) {
        return true;
    }
    else {
        return false;
    }
}

void BufferResourceCache::createTexelBuffer(const st::ShaderResource* texel_buffer, bool storage) {
    auto& group = buffers[texel_buffer->ParentGroupName()];
    auto buffer = std::make_unique<vpr::Buffer>(device);
    auto flags = storage ? VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT : VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT;
    auto& rsrc_context = ResourceContext::Get();
    buffer->CreateBuffer(flags, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, texel_buffer->MemoryRequired());
    buffer->CreateView(texel_buffer->Format(), buffer->Size(), 0);
    group.emplace(texel_buffer->Name(), std::move(buffer));
}

void BufferResourceCache::createUniformBuffer(const st::ShaderResource* uniform_buffer) {
    auto& group = buffers[uniform_buffer->ParentGroupName()];
    auto buffer = std::make_unique<vpr::Buffer>(device);
    auto flags = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    buffer->CreateBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, flags, uniform_buffer->MemoryRequired());
    group.emplace(uniform_buffer->Name(), std::move(buffer));
}

void BufferResourceCache::createStorageBuffer(const st::ShaderResource* storage_buffer) {
    auto& group = buffers[storage_buffer->ParentGroupName()];
    auto buffer = std::make_unique<vpr::Buffer>(device);
    buffer->CreateBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, storage_buffer->MemoryRequired());
    group.emplace(storage_buffer->Name(), std::move(buffer));
}

void BufferResourceCache::createResources(const std::vector<const st::ShaderResource*>& resources) {
    for (const auto& rsrc : resources) {
        if (!HasResource(rsrc->ParentGroupName(), rsrc->Name())) {
            createResource(rsrc);
        }
    }
}

void BufferResourceCache::createResource(const st::ShaderResource* rsrc) {
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
    default:
        break;
    }
}
