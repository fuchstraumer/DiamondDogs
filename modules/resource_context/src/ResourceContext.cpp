#include "ResourceContext.hpp"
#include "TransferSystem.hpp"
#include "ResourceLoader.hpp"
#include "Allocator.hpp"
#include "LogicalDevice.hpp"
#include "PhysicalDevice.hpp"
#include "vkAssert.hpp"
#include "UploadBuffer.hpp"
#include <vector>
#include <algorithm>
#include "easylogging++.h"

static VkAccessFlags accessFlagsFromBufferUsage(VkBufferUsageFlags usage_flags) {
    if (usage_flags & VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT) {
        return VK_ACCESS_UNIFORM_READ_BIT;
    }
    else if (usage_flags & VK_BUFFER_USAGE_INDEX_BUFFER_BIT) {
        return VK_ACCESS_INDEX_READ_BIT;
    }
    else if (usage_flags & VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) {
        return VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
    }
    else if (usage_flags & VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT) {
        return VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
    }
    else if (usage_flags & VK_BUFFER_USAGE_CONDITIONAL_RENDERING_BIT_EXT) {
        return VK_ACCESS_CONDITIONAL_RENDERING_READ_BIT_EXT;
    }
    else if ((usage_flags & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) || (usage_flags & VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT)) { 
        return VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    }
    else if (usage_flags & VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT) {
        return VK_ACCESS_SHADER_READ_BIT;
    }
    else {
        return VK_ACCESS_MEMORY_READ_BIT;
    }
}

static VkAccessFlags accessFlagsFromImageUsage(const VkImageUsageFlags usage_flags) {
    if (usage_flags & VK_IMAGE_USAGE_SAMPLED_BIT) {
        return VK_ACCESS_SHADER_READ_BIT;
    }
    else if (usage_flags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) {
        return VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    }
    else if (usage_flags & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) {
        return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    }
    else if (usage_flags & VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT) {
        return VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
    }
    else {
        return VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_MEMORY_READ_BIT;
    }
}

static VkImageLayout imageLayoutFromUsage(const VkImageUsageFlags usage_flags) {
    if (usage_flags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) {
        return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }
    else if (usage_flags & VK_IMAGE_USAGE_SAMPLED_BIT) {
        return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    else if (usage_flags & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) {
        return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    }
    else {
        return VK_IMAGE_LAYOUT_GENERAL;
    }
}

static vpr::Allocator::allocation_extensions getExtensionFlags(const vpr::Device* device) {
    return device->DedicatedAllocationExtensionsEnabled() ? vpr::Allocator::allocation_extensions::DedicatedAllocations : vpr::Allocator::allocation_extensions::None;
}

ResourceContext::ResourceContext() : device(nullptr), allocator(nullptr) {}

ResourceContext::~ResourceContext() {
    Destroy();
}

ResourceContext & ResourceContext::Get() {
    static ResourceContext context;
    return context;
}

void ResourceContext::Construct(vpr::Device * _device, vpr::PhysicalDevice * physical_device) {
    device = _device;
    allocator = std::make_unique<vpr::Allocator>(_device->vkHandle(), physical_device->vkHandle(), getExtensionFlags(_device));
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(physical_device->vkHandle(), &properties);
    UploadBuffer::NonCoherentAtomSize = properties.limits.nonCoherentAtomSize;
    auto& transfer_system = ResourceTransferSystem::GetTransferSystem();
    transfer_system.Initialize(_device, allocator.get());

}

void ResourceContext::Destroy() {
    std::lock_guard eraseGuard(containerMutex);
    for (auto iter = resources.cbegin(); iter != resources.cend();) {
        // Erased iterator gets invalidated, so use post-increment to return
        // current entry we want to erase, and increment iterator to be next 
        // entry to erase. Otherwise, we get serious errors and issues.
        auto iter_copy = iter++;
        destroyResource(iter_copy);
    }
    allocator.reset();
}

VulkanResource* ResourceContext::CreateBuffer(const VkBufferCreateInfo* info, const VkBufferViewCreateInfo* view_info, const size_t num_data, const gpu_resource_data_t* initial_data, const memory_type _memory_type, void* user_data) {
    VulkanResource* resource = nullptr;
    {
        std::lock_guard<std::mutex> emplaceGuard(containerMutex);
        auto iter = resources.emplace(std::make_unique<VulkanResource>());
        resource = iter.first->get();
        auto info_iter = resourceInfos.bufferInfos.emplace(resource, *info);
        resource->Type = resource_type::BUFFER;
        resource->Info = &info_iter.first->second;
        resource->UserData = user_data;
    }

    if ((_memory_type == memory_type::DEVICE_LOCAL) && (num_data != 0)) {
        // Device local buffer that will be transferred into, make sure it has the requisite flag.
        VkBufferCreateInfo* buffer_info = reinterpret_cast<VkBufferCreateInfo*>(resource->Info);
        if (!(buffer_info->usage & VK_BUFFER_USAGE_TRANSFER_DST_BIT)) {
            LOG(WARNING) << "Buffer requiring transfer_dst usage flags did not have usage flags set! Updating now...";
        }
        buffer_info->usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    }
    
    VkResult result = vkCreateBuffer(device->vkHandle(), reinterpret_cast<VkBufferCreateInfo*>(resource->Info), nullptr, reinterpret_cast<VkBuffer*>(&resource->Handle));
    VkAssert(result);

    resourceInfos.resourceMemoryType.emplace(resource, _memory_type);
    auto alloc_iter = resourceAllocations.emplace(resource, vpr::Allocation());
    vpr::Allocation& alloc = alloc_iter.first->second;
    allocator->AllocateForBuffer(reinterpret_cast<VkBuffer&>(resource->Handle), getAllocReqs(_memory_type), vpr::AllocationType::Buffer, alloc);

    if (view_info) {
        auto view_iter = resourceInfos.bufferViewInfos.emplace(resource, *view_info);
        resource->ViewInfo = &view_iter.first->second;
        VkBufferViewCreateInfo* local_view_info = reinterpret_cast<VkBufferViewCreateInfo*>(resource->ViewInfo);
        local_view_info->buffer = (VkBuffer)resource->Handle;
        result = vkCreateBufferView(device->vkHandle(), local_view_info, nullptr, reinterpret_cast<VkBufferView*>(&resource->ViewHandle));
        VkAssert(result);
    }

    if (initial_data) {
        if ((_memory_type == memory_type::HOST_VISIBLE) || (_memory_type == memory_type::HOST_VISIBLE_AND_COHERENT)) {
            setBufferInitialDataHostOnly(resource, num_data, initial_data, alloc, _memory_type);
        }
        else {
            setBufferInitialDataUploadBuffer(resource, num_data, initial_data, alloc);
        }
    }

    return resource;
}

void ResourceContext::SetBufferData(VulkanResource* dest_buffer, const size_t num_data, const gpu_resource_data_t* data) {
    memory_type mem_type = resourceInfos.resourceMemoryType.at(dest_buffer);
    if ((mem_type == memory_type::HOST_VISIBLE) || (mem_type == memory_type::HOST_VISIBLE_AND_COHERENT)) {
        setBufferInitialDataHostOnly(dest_buffer, num_data, data, resourceAllocations.at(dest_buffer), mem_type);
    }
    else {
        setBufferInitialDataUploadBuffer(dest_buffer, num_data, data, resourceAllocations.at(dest_buffer));
    }
}

void ResourceContext::FillBuffer(VulkanResource * dest_buffer, const uint32_t value, const size_t offset, const size_t fill_size) {
    auto& transfer_system = ResourceTransferSystem::GetTransferSystem();
    auto guard = transfer_system.AcquireSpinLock();
    auto cmd = transfer_system.TransferCmdBuffer();
    vkCmdFillBuffer(cmd, (VkBuffer)dest_buffer->Handle, offset, fill_size, value);
}

VulkanResource* ResourceContext::CreateImage(const VkImageCreateInfo* info, const VkImageViewCreateInfo* view_info, const size_t num_data, const gpu_image_resource_data_t* initial_data, const memory_type _memory_type, void* user_data) {
    VulkanResource* resource = nullptr;

    {
        std::lock_guard<std::mutex> emplaceGuard(containerMutex);
        auto iter = resources.emplace(std::make_unique<VulkanResource>());
        resource = iter.first->get();
        auto info_iter = resourceInfos.imageInfos.emplace(resource, *info);
        resource->Type = resource_type::IMAGE;
        resource->Info = &info_iter.first->second;
        resource->UserData = user_data;
        resourceInfos.resourceMemoryType.emplace(resource, _memory_type);
    }

    // This probably isn't ideal but it's a reasonable assumption to make.
    VkImageCreateInfo* create_info = reinterpret_cast<VkImageCreateInfo*>(resource->Info);
    if (!(create_info->usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)) {
        create_info->usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    }
    VkResult result = vkCreateImage(device->vkHandle(), create_info, nullptr, reinterpret_cast<VkImage*>(&resource->Handle));
    VkAssert(result);

    vpr::AllocationType alloc_type;
    VkImageTiling format_tiling = device->GetFormatTiling(info->format, featureFlagsFromUsage(info->usage));
    if (format_tiling == VK_IMAGE_TILING_LINEAR) {
        alloc_type = vpr::AllocationType::ImageLinear;
    }
    else if (format_tiling == VK_IMAGE_TILING_OPTIMAL) {
        alloc_type = vpr::AllocationType::ImageTiled;
    }
    else {
        alloc_type = vpr::AllocationType::Unknown;
    }

    auto alloc_iter = resourceAllocations.emplace(resource, vpr::Allocation());
    vpr::Allocation& alloc = alloc_iter.first->second;
    allocator->AllocateForImage(reinterpret_cast<VkImage&>(resource->Handle), getAllocReqs(_memory_type), alloc_type, alloc);


    if (view_info) {
        auto view_iter = resourceInfos.imageViewInfos.emplace(resource, *view_info);
        resource->ViewInfo = &view_iter.first->second;
        VkImageViewCreateInfo* local_view_info = reinterpret_cast<VkImageViewCreateInfo*>(resource->ViewInfo);
        local_view_info->image = (VkImage)resource->Handle;
        result = vkCreateImageView(device->vkHandle(), local_view_info, nullptr, reinterpret_cast<VkImageView*>(&resource->ViewHandle));
        VkAssert(result);
    }

    if (initial_data) {
        setImageInitialData(resource, num_data, initial_data, alloc);
    }

    return resource;
}

VulkanResource * ResourceContext::CreateImageView(const VulkanResource * base_rsrc, const VkImageViewCreateInfo * view_info, void * user_data) {
    auto iter = std::find_if(std::cbegin(resources), std::cend(resources), [base_rsrc](const std::unique_ptr<VulkanResource>& rsrc) {
        return base_rsrc == rsrc.get();
    });

    if (iter != std::cend(resources)) {
        VulkanResource* found_resource = iter->get();
        VulkanResource* result = nullptr;

        {
            std::lock_guard<std::mutex> guard(containerMutex);
            auto iter = resources.emplace(std::make_unique<VulkanResource>());
            result = iter.first->get();
            imageViews.emplace(found_resource, result);
            resourceInfos.imageViewInfos.emplace(result, *view_info);
            resourceInfos.imageInfos.emplace(result, *reinterpret_cast<VkImageCreateInfo*>(found_resource->Info));
        }

        VkImageViewCreateInfo* updated_view_info = &resourceInfos.imageViewInfos.at(result);
        updated_view_info->image = (VkImage)found_resource->Handle;
        VkResult res = vkCreateImageView(device->vkHandle(), updated_view_info, nullptr, (VkImageView*)result->ViewHandle);
        VkAssert(res);
        result->Handle = 0;

        return result;
    }
    else {
        return nullptr;
    }
}

void ResourceContext::SetImageData(VulkanResource* image, const size_t num_data, const gpu_image_resource_data_t* data) {
    setImageInitialData(image, num_data, data, resourceAllocations.at(image));
}

VulkanResource* ResourceContext::CreateSampler(const VkSamplerCreateInfo* info, void* user_data) {
    VulkanResource* resource = nullptr; 
    {
        std::lock_guard<std::mutex> emplaceGuard(containerMutex);
        auto iter = resources.emplace(std::make_unique<VulkanResource>());
        resource = iter.first->get();
    }

    auto info_iter = resourceInfos.samplerInfos.emplace(resource, *info);
    resource->Type = resource_type::SAMPLER;
    resource->Info = &info_iter.first->second;
    resource->UserData = user_data;

    VkResult result = vkCreateSampler(device->vkHandle(), info, nullptr, reinterpret_cast<VkSampler*>(&resource->Handle));
    VkAssert(result);

    return resource;
}

VulkanResource* ResourceContext::CreateCombinedImageSampler(const VkImageCreateInfo * info, const VkImageViewCreateInfo * view_info, const VkSamplerCreateInfo * sampler_info, 
    const size_t num_data, const gpu_image_resource_data_t * initial_data, const memory_type _memory_type, void * user_data) {
    VulkanResource* resource = CreateImage(info, view_info, num_data, initial_data, _memory_type, user_data);
    resource->Type = resource_type::COMBINED_IMAGE_SAMPLER;
    resource->Sampler = CreateSampler(sampler_info);
    return resource;
}

VulkanResource* ResourceContext::CreateResourceCopy(VulkanResource * src) {
    VulkanResource* result = nullptr;
    CopyResource(src, &result);
    return result;
}

void ResourceContext::CopyResource(VulkanResource * src, VulkanResource** dest) {
    switch (src->Type) {
    case resource_type::BUFFER:
        createBufferResourceCopy(src, dest);
        break;
    case resource_type::SAMPLER:
        createSamplerResourceCopy(src, dest);
        break;
    case resource_type::IMAGE:
        createImageResourceCopy(src, dest);
        break;
    case resource_type::COMBINED_IMAGE_SAMPLER:
        createCombinedImageSamplerResourceCopy(src, dest);
        break;
    default:
        throw std::domain_error("Passed source resource to CopyResource had invalid resource_type value.");
    };
}

void ResourceContext::CopyResourceContents(VulkanResource* src, VulkanResource* dst) {
    if (src->Type == dst->Type) {
        switch (src->Type) {
        case resource_type::BUFFER:
            break;
        case resource_type::IMAGE:
            break;
        case resource_type::COMBINED_IMAGE_SAMPLER:
            break;
        case resource_type::SAMPLER:
            [[fallthrough]];
        default:
            break;
        }
    }
    else {

    }
}

void ResourceContext::DestroyResource(VulkanResource * rsrc) {
    std::lock_guard<std::mutex> eraseGuard(containerMutex);
    auto iter = std::find_if(std::begin(resources), std::end(resources), [rsrc](const decltype(resources)::value_type& entry) {
        return entry.get() == rsrc;
    });
    if (iter == std::end(resources)) {
        LOG(ERROR) << "Tried to erase resource that isn't in internal containers!";
        throw std::runtime_error("Tried to erase resource that isn't in internal containers!");
    }
    else {
        destroyResource(iter);
    }
}

void* ResourceContext::MapResourceMemory(VulkanResource* resource, size_t size, size_t offset) {
    void* mapped_ptr = nullptr;
    auto& alloc = resourceAllocations.at(resource);
    if (resourceInfos.resourceMemoryType.at(resource) == memory_type::HOST_VISIBLE) {
        mappedRanges[resource] = VkMappedMemoryRange{ VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE, nullptr, alloc.Memory(), alloc.Offset() + offset, size == 0 ? alloc.Size : size }; 
        VkResult result = vkInvalidateMappedMemoryRanges(device->vkHandle(), 1, &mappedRanges[resource]);
        VkAssert(result);
    }
    alloc.Map(size == 0 ? alloc.Size : size, alloc.Offset() + offset, &mapped_ptr);
    return mapped_ptr;
}

void ResourceContext::UnmapResourceMemory(VulkanResource* resource) {
    resourceAllocations.at(resource).Unmap();
    if (resourceInfos.resourceMemoryType.at(resource) == memory_type::HOST_VISIBLE) {
        VkResult result = vkFlushMappedMemoryRanges(device->vkHandle(), 1, &mappedRanges[resource]);
        VkAssert(result);
    }
}

void ResourceContext::Update() {
    auto& transfer_system = ResourceTransferSystem::GetTransferSystem();
    transfer_system.CompleteTransfers();
}

void ResourceContext::setBufferInitialDataHostOnly(VulkanResource* resource, const size_t num_data, const gpu_resource_data_t* initial_data, vpr::Allocation& alloc, memory_type _memory_type) {
    void* mapped_address = nullptr;
    alloc.Map(alloc.Size, 0, &mapped_address);
    size_t offset = 0;
    for (size_t i = 0; i < num_data; ++i) {
        void* curr_address = (void*)((size_t)mapped_address + offset);
        memcpy(curr_address, initial_data[i].Data, initial_data[i].DataSize);
        offset += initial_data[i].DataSize;
    }
    alloc.Unmap();
    if (_memory_type == memory_type::HOST_VISIBLE) {
        VkMappedMemoryRange mapped_memory{ VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE, nullptr, alloc.Memory(), alloc.Offset(), VK_WHOLE_SIZE };
        VkResult result = vkFlushMappedMemoryRanges(device->vkHandle(), 1, &mapped_memory);
        VkAssert(result);
    }
}

void ResourceContext::setBufferInitialDataUploadBuffer(VulkanResource* resource, const size_t num_data, const gpu_resource_data_t* initial_data, vpr::Allocation& alloc) {

    /*
        Set everything up we need for recording the command ahead of time, before acquiring the transfer system lock.
        Then create a buffer, acquire the spinlock and populate buffer + record transfer commands, release spinlock.
    */

    const VkBufferCreateInfo* p_info = reinterpret_cast<VkBufferCreateInfo*>(resource->Info);
    const VkBufferMemoryBarrier memory_barrier0{
        VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        nullptr,
        0,
        VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_QUEUE_FAMILY_IGNORED,
        VK_QUEUE_FAMILY_IGNORED,
        reinterpret_cast<VkBuffer>(resource->Handle),
        0,
        p_info->size
    };
    const VkBufferMemoryBarrier memory_barrier1{
        VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        nullptr,
        VK_ACCESS_TRANSFER_WRITE_BIT,
        accessFlagsFromBufferUsage(p_info->usage),
        VK_QUEUE_FAMILY_IGNORED,
        VK_QUEUE_FAMILY_IGNORED,
        (VkBuffer)resource->Handle,
        0,
        p_info->size
    };

    {
        auto& transfer_system = ResourceTransferSystem::GetTransferSystem();
        auto cmd = transfer_system.TransferCmdBuffer();
        auto guard = transfer_system.AcquireSpinLock();
        UploadBuffer* upload_buffer = transfer_system.CreateUploadBuffer(p_info->size);
        std::vector<VkBufferCopy> buffer_copies(num_data);
        VkDeviceSize offset = 0;
        for (size_t i = 0; i < num_data; ++i) {
            upload_buffer->SetData(initial_data[i].Data, initial_data[i].DataSize, offset);
            buffer_copies[i].size = initial_data[i].DataSize;
            buffer_copies[i].dstOffset = offset;
            buffer_copies[i].srcOffset = offset;
            offset += initial_data[i].DataSize;
        }

        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, 0, 1, &memory_barrier0, 0, nullptr);
        vkCmdCopyBuffer(cmd, upload_buffer->Buffer, reinterpret_cast<VkBuffer>(resource->Handle), static_cast<uint32_t>(buffer_copies.size()), buffer_copies.data());
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 1, &memory_barrier1, 0, nullptr);
    }
}

void ResourceContext::setImageInitialData(VulkanResource* resource, const size_t num_data, const gpu_image_resource_data_t* initial_data, vpr::Allocation& alloc) {

    const VkImageCreateInfo* info = reinterpret_cast<VkImageCreateInfo*>(resource->Info);

    const VkImageMemoryBarrier barrier0{
        VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        nullptr,
        0,
        VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        device->QueueFamilyIndices().Transfer,
        device->QueueFamilyIndices().Transfer,
        reinterpret_cast<VkImage>(resource->Handle),
        VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, info->mipLevels, 0, info->arrayLayers }
    };

    const VkImageMemoryBarrier barrier1{
            VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            nullptr,
            VK_ACCESS_TRANSFER_WRITE_BIT,
            accessFlagsFromImageUsage(info->usage),
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            imageLayoutFromUsage(info->usage),
            device->QueueFamilyIndices().Transfer,
            device->QueueFamilyIndices().Graphics,
            reinterpret_cast<VkImage>(resource->Handle),
            VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, info->mipLevels, 0, info->arrayLayers }
    };


    auto& transfer_system = ResourceTransferSystem::GetTransferSystem();
    {
        auto guard = transfer_system.AcquireSpinLock();
        UploadBuffer* upload_buffer = transfer_system.CreateUploadBuffer(alloc.Size);
        VkCommandBuffer cmd = transfer_system.TransferCmdBuffer();
        std::vector<VkBufferImageCopy> buffer_image_copies;
        size_t copy_offset = 0;
        for (uint32_t i = 0; i < num_data; ++i) {
            buffer_image_copies.emplace_back(VkBufferImageCopy{
                copy_offset,
                0,
                0,
                VkImageSubresourceLayers{ VK_IMAGE_ASPECT_COLOR_BIT, initial_data[i].MipLevel, initial_data[i].ArrayLayer, initial_data[i].NumLayers },
                VkOffset3D{ 0, 0, 0 },
                VkExtent3D{ initial_data[i].Width, initial_data[i].Height, 1 }
                });
#ifndef NDEBUG
            assert(initial_data[i].MipLevel < info->mipLevels);
            assert(initial_data[i].ArrayLayer < info->arrayLayers);
#endif // DEBUG
            upload_buffer->SetData(initial_data[i].Data, initial_data[i].DataSize, copy_offset);
            copy_offset += initial_data[i].DataSize;
        }
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier0);
        vkCmdCopyBufferToImage(cmd, upload_buffer->Buffer, reinterpret_cast<VkImage>(resource->Handle), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, static_cast<uint32_t>(buffer_image_copies.size()), buffer_image_copies.data());
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier1);
    }

}

vpr::AllocationRequirements ResourceContext::getAllocReqs(memory_type _memory_type) const noexcept {
    vpr::AllocationRequirements alloc_reqs;
    switch (_memory_type) {
    case memory_type::HOST_VISIBLE:
        alloc_reqs.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
        break;
    case memory_type::HOST_VISIBLE_AND_COHERENT:
        alloc_reqs.requiredFlags = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
        break;
    case memory_type::DEVICE_LOCAL:
        alloc_reqs.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        break;
    case memory_type::SPARSE:
        break;
    default:
        break;
    }
    return alloc_reqs;
}

VkFormatFeatureFlags ResourceContext::featureFlagsFromUsage(const VkImageUsageFlags flags) const noexcept {
    VkFormatFeatureFlags result = 0;
    if (flags & VK_IMAGE_USAGE_SAMPLED_BIT) {
        result |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    }
    if (flags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) {
        result |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
    }
    if (flags & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) {
        result |= VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
    }
    if (flags & VK_IMAGE_USAGE_STORAGE_BIT) {
        result |= VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
    }
    return result;
}

void ResourceContext::createBufferResourceCopy(VulkanResource * src, VulkanResource** dst) {
    const VkBufferCreateInfo* create_info = reinterpret_cast<const VkBufferCreateInfo*>(src->Info);
    const VkBufferViewCreateInfo* view_info = nullptr;
    if (src->ViewHandle != VK_NULL_HANDLE) { 
        view_info = reinterpret_cast<const VkBufferViewCreateInfo*>(src->ViewInfo);
    }
    *dst = CreateBuffer(create_info, view_info, 0, nullptr, resourceInfos.resourceMemoryType.at(src), nullptr);
    CopyResourceContents(src, *dst);
}

void ResourceContext::createImageResourceCopy(VulkanResource * src, VulkanResource** dst) {
    const VkImageCreateInfo* image_info = reinterpret_cast<const VkImageCreateInfo*>(src->Info);
    const VkImageViewCreateInfo* view_info = nullptr;
    if (src->ViewHandle != VK_NULL_HANDLE) {
        view_info = reinterpret_cast<const VkImageViewCreateInfo*>(src->ViewInfo);
    }
    *dst = CreateImage(image_info, view_info, 0, nullptr, resourceInfos.resourceMemoryType.at(src), nullptr);
    CopyResourceContents(src, *dst);
}

void ResourceContext::createSamplerResourceCopy(VulkanResource * src, VulkanResource** dst) {
    const VkSamplerCreateInfo* sampler_info = reinterpret_cast<const VkSamplerCreateInfo*>(src->Info);
    *dst = CreateSampler(sampler_info, nullptr);
}

void ResourceContext::createCombinedImageSamplerResourceCopy(VulkanResource* src, VulkanResource** dest) {
    createImageResourceCopy(src, dest);
    VulkanResource** sampler_to_create = &(*dest)->Sampler;
    createSamplerResourceCopy(src->Sampler, sampler_to_create);
    (*dest)->Type = resource_type::COMBINED_IMAGE_SAMPLER;
}

void ResourceContext::copyBufferContentsToBuffer(VulkanResource* src, VulkanResource* dst) {

    const VkBufferCreateInfo* src_info = reinterpret_cast<const VkBufferCreateInfo*>(src->Info);
    const VkBufferCreateInfo* dst_info = reinterpret_cast<const VkBufferCreateInfo*>(dst->Info);
    assert(src_info->size <= dst_info->size);
    const VkBufferCopy copy{
        0,
        0,
        src_info->size
    };

    const VkBufferMemoryBarrier pre_transfer_barriers[2] {
        VkBufferMemoryBarrier{
            VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            nullptr,
            accessFlagsFromBufferUsage(src_info->usage),
            VK_ACCESS_TRANSFER_READ_BIT,
            VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED,
            (VkBuffer)src->Handle,
            0,
            src_info->size
        },
        VkBufferMemoryBarrier{
            VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            nullptr,
            accessFlagsFromBufferUsage(dst_info->usage),
            VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED,
            (VkBuffer)dst->Handle,
            0,
            dst_info->size
        }
    };

    const VkBufferMemoryBarrier post_transfer_barriers[2] {
        VkBufferMemoryBarrier{
            VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            nullptr,
            VK_ACCESS_TRANSFER_READ_BIT,
            accessFlagsFromBufferUsage(src_info->usage),
            VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED,
            (VkBuffer)src->Handle,
            0,
            src_info->size
        },
        VkBufferMemoryBarrier{
            VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            nullptr,
            VK_ACCESS_TRANSFER_WRITE_BIT,
            accessFlagsFromBufferUsage(dst_info->usage),
            VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED,
            (VkBuffer)dst->Handle,
            0,
            dst_info->size
        }
    };

    {
        auto& transfer_system = ResourceTransferSystem::GetTransferSystem();
        auto guard = transfer_system.AcquireSpinLock();
        auto cmd = transfer_system.TransferCmdBuffer();

        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 2, pre_transfer_barriers, 0, nullptr);
        vkCmdCopyBuffer(cmd, (VkBuffer)src->Handle, (VkBuffer)dst->Handle, 1, &copy);
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 2, post_transfer_barriers, 0, nullptr);
    }
}

void ResourceContext::copyImageContentsToImage(VulkanResource* src, VulkanResource* dst) {
    throw std::runtime_error("Not yet implemented!");
}

void ResourceContext::copyBufferContentsToImage(VulkanResource* src, VulkanResource* dst) {
    throw std::runtime_error("Not yet implemented!");
}

void ResourceContext::copyImageContentsToBuffer(VulkanResource* src, VulkanResource* dst) {
    throw std::runtime_error("Not yet implemented!");
}

void ResourceContext::destroyResource(resource_iter_t iter) {
    switch (iter->get()->Type) {
    case resource_type::BUFFER:
        destroyBuffer(iter);
        break;
    case resource_type::IMAGE:
        destroyImage(iter);
        break;
    case resource_type::SAMPLER:
        destroySampler(iter);
        break;
    case resource_type::COMBINED_IMAGE_SAMPLER:
        DestroyResource(iter->get()->Sampler);
        destroyImage(iter);
        break;
    case resource_type::INVALID:
        [[fallthrough]];
    default:
        throw std::runtime_error("Invalid resource type!");
    }
}

void ResourceContext::destroyBuffer(resource_iter_t iter) {
    VulkanResource* rsrc = iter->get();
    if (rsrc->ViewHandle != 0) {
        vkDestroyBufferView(device->vkHandle(), (VkBufferView)rsrc->ViewHandle, nullptr);
    }
    vkDestroyBuffer(device->vkHandle(), (VkBuffer)rsrc->Handle, nullptr);
    allocator->FreeMemory(&resourceAllocations.at(rsrc)); 
    resources.erase(iter);
    resourceInfos.bufferInfos.erase(rsrc);
    resourceInfos.bufferViewInfos.erase(rsrc);
    resourceAllocations.erase(rsrc);
}

void ResourceContext::destroyImage(resource_iter_t iter) {
    VulkanResource* rsrc = iter->get();
    if (rsrc->ViewHandle != 0) {
        vkDestroyImageView(device->vkHandle(), (VkImageView)rsrc->ViewHandle, nullptr);
    }
    vkDestroyImage(device->vkHandle(), (VkImage)rsrc->Handle, nullptr);
    allocator->FreeMemory(&resourceAllocations.at(rsrc));
    resources.erase(iter);
    resourceInfos.imageInfos.erase(rsrc);
    resourceInfos.imageViewInfos.erase(rsrc);
    resourceAllocations.erase(rsrc);
}

void ResourceContext::destroySampler(resource_iter_t iter) {
    VulkanResource* rsrc = iter->get();
    vkDestroySampler(device->vkHandle(), (VkSampler)rsrc->Handle, nullptr);
    resourceInfos.samplerInfos.erase(rsrc);
    resources.erase(iter);
}
