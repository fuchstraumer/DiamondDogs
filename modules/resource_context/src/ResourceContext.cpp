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
#include <unordered_set>
#include <unordered_map>
#include <memory>
#include <string>
#include <variant>
#include <vulkan/vulkan.h>
#include <mutex>
#include <vk_mem_alloc.h>

enum class lock_mode : uint8_t
{
    Invalid = 0,
    Read = 1,
    Write = 2
};

/*
    \brief A variant of lock_guard for shared_mutex, that allows for different locking semantics based on the current needs.
    This is used in place of the mutex itself where possible, at it can greatly reduce the chance of mis-using mutexes in our code.
*/
struct rw_lock_guard
{

    rw_lock_guard(lock_mode _mode, std::shared_mutex& _mut) noexcept : mut(_mut), mode(_mode)
    {
        if (mode == lock_mode::Read)
        {
            mut.lock_shared();
        }
        else
        {
            mut.lock();
        }
    }

    ~rw_lock_guard() noexcept
    {
        if (mode == lock_mode::Read)
        {
            mut.unlock_shared();
        }
        else
        {
            mut.unlock();
        }
    }

    rw_lock_guard(rw_lock_guard&& other) noexcept : mut(std::move(other.mut)), mode(std::move(other.mode)) {}
    rw_lock_guard& operator=(rw_lock_guard&& other) = delete;
    rw_lock_guard(const rw_lock_guard&) = delete;
    rw_lock_guard& operator=(const rw_lock_guard&) = delete;

private:
    lock_mode mode{ lock_mode::Invalid };
    std::shared_mutex& mut;
};

struct ResourceContextImpl
{
    VulkanResource* createBuffer(const VkBufferCreateInfo* info, const VkBufferViewCreateInfo* view_info, const size_t num_data, const gpu_resource_data_t* initial_data, const resource_usage _resource_usage, const resource_creation_flags _flags, void* user_data = nullptr);
    void setBufferData(VulkanResource* dest_buffer, const size_t num_data, const gpu_resource_data_t* data);
    VulkanResource* createImage(const VkImageCreateInfo* info, const VkImageViewCreateInfo* view_info, const size_t num_data, const gpu_image_resource_data_t* initial_data, const resource_usage _resource_usage, const resource_creation_flags _flags, void* user_data = nullptr);
    VulkanResource* createImageView(const VulkanResource* base_rsrc, const VkImageViewCreateInfo* view_info, void* user_data = nullptr);
    VulkanResource* createSampler(const VkSamplerCreateInfo* info, void* user_data = nullptr);
    // using uint64_t for allocator handle to keep this header clean. we should probably move this to an impl pointer
    void setBufferInitialDataHostOnly(VulkanResource * resource, const size_t num_data, const gpu_resource_data_t * initial_data, uint64_t alloc, resource_usage _resource_usage);
    void setBufferInitialDataUploadBuffer(VulkanResource* resource, const size_t num_data, const gpu_resource_data_t* initial_data, uint64_t alloc);
    void setImageInitialData(VulkanResource* resource, const size_t num_data, const gpu_image_resource_data_t* initial_data, uint64_t alloc);
    vpr::AllocationRequirements getAllocReqs(resource_usage _resource_usage) const noexcept;
    VkFormatFeatureFlags featureFlagsFromUsage(const VkImageUsageFlags flags) const noexcept;

    std::unordered_set<std::unique_ptr<VulkanResource>> resources;
    using resource_iter_t = decltype(resources)::iterator;

    void createBufferResourceCopy(VulkanResource* src, VulkanResource** dst);
    void createImageResourceCopy(VulkanResource* src, VulkanResource** dst);
    void createSamplerResourceCopy(VulkanResource* src, VulkanResource** dst);
    void createCombinedImageSamplerResourceCopy(VulkanResource * src, VulkanResource ** dest);

    void copyBufferContentsToBuffer(VulkanResource * src, VulkanResource * dst);
    void copyImageContentsToImage(VulkanResource * src, VulkanResource * dst);
    void copyBufferContentsToImage(VulkanResource * src, VulkanResource * dst);
    void copyImageContentsToBuffer(VulkanResource * src, VulkanResource * dst);

    void destroyResource(resource_iter_t iter);
    void destroyBuffer(resource_iter_t iter);
    void destroyImage(resource_iter_t iter);
    void destroySampler(resource_iter_t iter);


    struct infoStorage {
        std::unordered_map<VulkanResource*, resource_usage> resourceMemoryType;
        std::unordered_map<VulkanResource*, VkBufferCreateInfo> bufferInfos;
        std::unordered_map<VulkanResource*, VkBufferViewCreateInfo> bufferViewInfos;
        std::unordered_map<VulkanResource*, VkImageCreateInfo> imageInfos;
        std::unordered_map<VulkanResource*, VkImageViewCreateInfo> imageViewInfos;
        std::unordered_map<VulkanResource*, VkSamplerCreateInfo> samplerInfos;
    } resourceInfos;

    std::unordered_map<VulkanResource*, std::string> resourceNames;
    std::unordered_map<VulkanResource*, uint64_t> resourceAllocations;
    // Resources that depend on the key for their Image handle, but which are still independent views
    // of the key, go here. When key is destroyed, we have to destroy all the views too.
    std::unordered_multimap<VulkanResource*, VulkanResource*> imageViews;
    std::unordered_map<uint64_t, std::unique_ptr<VmaAllocationInfo>> allocInfos;
    uint64_t allocatorHandle{ VK_NULL_HANDLE };
    std::shared_mutex containerMutex;
    const vpr::Device* device;

};

static VkAccessFlags accessFlagsFromBufferUsage(VkBufferUsageFlags usage_flags)
{
    if (usage_flags & VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)
    {
        return VK_ACCESS_UNIFORM_READ_BIT;
    }
    else if (usage_flags & VK_BUFFER_USAGE_INDEX_BUFFER_BIT)
    {
        return VK_ACCESS_INDEX_READ_BIT;
    }
    else if (usage_flags & VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)
    {
        return VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
    }
    else if (usage_flags & VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT)
    {
        return VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
    }
    else if (usage_flags & VK_BUFFER_USAGE_CONDITIONAL_RENDERING_BIT_EXT)
    {
        return VK_ACCESS_CONDITIONAL_RENDERING_READ_BIT_EXT;
    }
    else if ((usage_flags & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) || (usage_flags & VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT))
    { 
        return VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    }
    else if (usage_flags & VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT)
    {
        return VK_ACCESS_SHADER_READ_BIT;
    }
    else
    {
        return VK_ACCESS_MEMORY_READ_BIT;
    }
}

static VkAccessFlags accessFlagsFromImageUsage(const VkImageUsageFlags usage_flags)
{
    if (usage_flags & VK_IMAGE_USAGE_SAMPLED_BIT)
    {
        return VK_ACCESS_SHADER_READ_BIT;
    }
    else if (usage_flags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
    {
        return VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    }
    else if (usage_flags & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
    {
        return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    }
    else if (usage_flags & VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT)
    {
        return VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
    }
    else
    {
        return VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_MEMORY_READ_BIT;
    }
}

static VkImageLayout imageLayoutFromUsage(const VkImageUsageFlags usage_flags)
{
    if (usage_flags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
    {
        return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }
    else if (usage_flags & VK_IMAGE_USAGE_SAMPLED_BIT)
    {
        return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    else if (usage_flags & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
    {
        return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    }
    else
    {
        return VK_IMAGE_LAYOUT_GENERAL;
    }
}

VkMemoryPropertyFlags GetMemoryPropertyFlags(resource_usage _resource_usage) noexcept
{
    switch (_resource_usage)
    {
    case resource_usage::GPU_ONLY:
        return VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    case resource_usage::CPU_ONLY:
        return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    case resource_usage::CPU_TO_GPU:
        [[fallthrough]];
    case resource_usage::GPU_TO_CPU:
        return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    default:
        __assume(0);
    }
}

static void AllocatorDeleter(void* ptr) noexcept
{

}

static vpr::Allocator::allocation_extensions getExtensionFlags(const vpr::Device* device)
{
    return device->DedicatedAllocationExtensionsEnabled() ? vpr::Allocator::allocation_extensions::DedicatedAllocations : vpr::Allocator::allocation_extensions::None;
}

ResourceContext::ResourceContext() : impl(nullptr) {}

ResourceContext::~ResourceContext()
{
    Destroy();
}

ResourceContext & ResourceContext::Get()
{
    static ResourceContext context;
    return context;
}

void ResourceContext::Construct(vpr::Device* _device, vpr::PhysicalDevice* physical_device)
{
    device = _device;

    auto flags = getExtensionFlags(device);

    VmaAllocatorCreateInfo create_info
    {
        flags == vpr::Allocator::allocation_extensions::DedicatedAllocations ? VMA_ALLOCATOR_CREATE_KHR_DEDICATED_ALLOCATION_BIT : 0u,
        physical_device->vkHandle(),
        device->vkHandle(),
        0u,
        nullptr,
        nullptr,
        1u,
        nullptr,
        nullptr,
        nullptr
    };

    VkResult result = vmaCreateAllocator(&create_info, reinterpret_cast<VmaAllocator*>(&allocatorHandle));
    VkAssert(result);
    
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(physical_device->vkHandle(), &properties);
    UploadBuffer::NonCoherentAtomSize = properties.limits.nonCoherentAtomSize;
    auto& transfer_system = ResourceTransferSystem::GetTransferSystem();
    //transfer_system.Initialize(_device, allocator.get());

}

void ResourceContext::Destroy()
{
    rw_lock_guard destruction_guard(lock_mode::Write, containerMutex);
    for (auto iter = resources.cbegin(); iter != resources.cend();)
    {
        // Erased iterator gets invalidated, so use post-increment to return
        // current entry we want to erase, and increment iterator to be next 
        // entry to erase. Otherwise, we get serious errors and issues.
        auto iter_copy = iter++;
        destroyResource(iter_copy);
    }
    
    vmaDestroyAllocator((VmaAllocator)allocatorHandle);
}

void ResourceContext::FillBuffer(VulkanResource * dest_buffer, const uint32_t value, const size_t offset, const size_t fill_size)
{
    auto& transfer_system = ResourceTransferSystem::GetTransferSystem();
    auto guard = transfer_system.AcquireSpinLock();
    auto cmd = transfer_system.TransferCmdBuffer();
    vkCmdFillBuffer(cmd, (VkBuffer)dest_buffer->Handle, offset, fill_size, value);
}

void ResourceContext::SetImageData(VulkanResource* image, const size_t num_data, const gpu_image_resource_data_t* data) 
{
    impl->setImageInitialData(image, num_data, data, resourceAllocations.at(image));
}

VulkanResource* ResourceContext::CreateSampler(const VkSamplerCreateInfo* info, void* user_data) {
    VulkanResource* resource = nullptr; 
    {
        std::lock_guard emplaceGuard(containerMutex);
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
    const size_t num_data, const gpu_image_resource_data_t * initial_data, const resource_usage _resource_usage, const resource_creation_flags _flags, void * user_data) 
{
    VulkanResource* resource = CreateImage(info, view_info, num_data, initial_data, _resource_usage, _flags, user_data);
    resource->Type = resource_type::COMBINED_IMAGE_SAMPLER;
    resource->Sampler = CreateSampler(sampler_info);
    return resource;
}

VulkanResource* ResourceContext::CreateResourceCopy(VulkanResource * src) 
{
    VulkanResource* result = nullptr;
    CopyResource(src, &result);
    return result;
}

void ResourceContext::CopyResource(VulkanResource * src, VulkanResource** dest) 
{
    switch (src->Type) 
    {
    case resource_type::BUFFER:
        impl->createBufferResourceCopy(src, dest);
        break;
    case resource_type::SAMPLER:
        impl->createSamplerResourceCopy(src, dest);
        break;
    case resource_type::IMAGE:
        impl->createImageResourceCopy(src, dest);
        break;
    case resource_type::COMBINED_IMAGE_SAMPLER:
        impl->createCombinedImageSamplerResourceCopy(src, dest);
        break;
    default:
        throw std::domain_error("Passed source resource to CopyResource had invalid resource_type value.");
    };
}

void ResourceContext::CopyResourceContents(VulkanResource* src, VulkanResource* dst) 
{
    if (src->Type == dst->Type) 
    {
        switch (src->Type)
        {
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
    else 
    {

    }
}

void ResourceContext::DestroyResource(VulkanResource * rsrc) 
{
    decltype(resources)::const_iterator iter;
    {
        rw_lock_guard erase_guard(lock_mode::Read, containerMutex); // only need read to find it
        iter = std::find_if(std::begin(resources), std::end(resources), [rsrc](const decltype(resources)::value_type& entry) {
            return entry.get() == rsrc;
        });
    }

    if (iter == std::end(resources)) 
    {
        LOG(ERROR) << "Tried to erase resource that isn't in internal containers!";
        throw std::runtime_error("Tried to erase resource that isn't in internal containers!");
    }
    else 
    {
        // issue: how to deal with recursive calls? Will try to doubly-lock when destroying combined image samplers
        destroyResource(iter);
    }
}

void* ResourceContext::MapResourceMemory(VulkanResource* resource, size_t size, size_t offset) 
{
    const VmaAllocator allocator = (VmaAllocator)allocatorHandle;
    VmaAllocation alloc;
    resource_usage alloc_usage;

    {
        rw_lock_guard lock_guard(lock_mode::Read, containerMutex);
        alloc = reinterpret_cast<VmaAllocation>(resourceAllocations.at(resource));
        alloc_usage = resourceInfos.resourceMemoryType.at(resource);
        if (alloc_usage == resource_usage::GPU_TO_CPU)
        {
            vmaInvalidateAllocation(allocator, alloc, offset, size == 0u ? VK_WHOLE_SIZE : size);
        }
    }

    void* mapped_ptr = nullptr;
    VkResult result = vmaMapMemory(allocator, alloc, &mapped_ptr);
    VkAssert(result);
    return mapped_ptr;
}

void ResourceContext::UnmapResourceMemory(VulkanResource* resource, size_t size, size_t offset) 
{
    const VmaAllocator allocator = (VmaAllocator)allocatorHandle;
    VmaAllocation alloc;
    resource_usage alloc_usage;
    {
        rw_lock_guard lock_guard(lock_mode::Read, containerMutex);
        alloc = reinterpret_cast<VmaAllocation>(resourceAllocations.at(resource));
        alloc_usage = resourceInfos.resourceMemoryType.at(resource);
    }

    vmaUnmapMemory(allocator, alloc);
    if (alloc_usage == resource_usage::CPU_TO_GPU)
    {
        vmaFlushAllocation(allocator, alloc, offset, size == 0u ? VK_WHOLE_SIZE : size);
    }
}

void ResourceContext::Update() {
    auto& transfer_system = ResourceTransferSystem::GetTransferSystem();
    transfer_system.CompleteTransfers();
}

VulkanResource* ResourceContextImpl::createBuffer(const VkBufferCreateInfo* info, const VkBufferViewCreateInfo* view_info, const size_t num_data, const gpu_resource_data_t* initial_data, const resource_usage _resource_usage, const resource_creation_flags _flags, void* user_data)
{
    VulkanResource* resource{ nullptr };
    VmaAllocationInfo* alloc_info{ nullptr };
    VmaAllocation alloc{ VK_NULL_HANDLE };

    {
        // write to containers now then just read from em for the rest
        rw_lock_guard emplace_guard(lock_mode::Write, containerMutex);
        auto iter = resources.emplace(std::make_unique<VulkanResource>());
        resource = iter.first->get();
        auto info_iter = resourceInfos.bufferInfos.emplace(resource, *info);
        resource->Type = resource_type::BUFFER;
        resource->Info = &info_iter.first->second;
        resource->UserData = user_data;
        auto alloc_info_iter = allocInfos.emplace(resource, std::make_unique<VmaAllocationInfo>());
        alloc_info = alloc_info_iter.first->second.get();
        resourceInfos.resourceMemoryType.emplace(resource, _resource_usage);
        auto alloc_iter = resourceAllocations.emplace(resource, VK_NULL_HANDLE);
        alloc = reinterpret_cast<VmaAllocation>(alloc_iter.first->second);
        if (view_info)
        {

        }
    }

    if ((_resource_usage == resource_usage::CPU_TO_GPU || _resource_usage == resource_usage::GPU_TO_CPU || _resource_usage == resource_usage::GPU_ONLY) && (num_data != 0))
    {
        // Device local buffer that will be transferred into, make sure it has the requisite flag.
        VkBufferCreateInfo* buffer_info = reinterpret_cast<VkBufferCreateInfo*>(resource->Info);
#ifndef NDEBUG
        if (!(buffer_info->usage & VK_BUFFER_USAGE_TRANSFER_DST_BIT))
        {
            LOG(WARNING) << "Buffer requiring transfer_dst usage flags did not have usage flags set! Updating now...";
        }
#endif
        buffer_info->usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    }

    VmaAllocationCreateInfo alloc_create_info
    {
        (VmaAllocationCreateFlags)_flags,
        (VmaMemoryUsage)_resource_usage,
        GetMemoryPropertyFlags(_resource_usage),
        0u,
        UINT32_MAX,
        VK_NULL_HANDLE,
        user_data
    };

    VkResult result = vmaCreateBuffer((VmaAllocator)allocatorHandle, reinterpret_cast<VkBufferCreateInfo*>(resource->Info), &alloc_create_info,
        reinterpret_cast<VkBuffer*>(&resource->Handle), &alloc, alloc_info);
    VkAssert(result);

    if (view_info)
    {
        auto view_iter = resourceInfos.bufferViewInfos.emplace(resource, *view_info);
        resource->ViewInfo = &view_iter.first->second;
        VkBufferViewCreateInfo* local_view_info = reinterpret_cast<VkBufferViewCreateInfo*>(resource->ViewInfo);
        local_view_info->buffer = (VkBuffer)resource->Handle;
        result = vkCreateBufferView(device->vkHandle(), local_view_info, nullptr, reinterpret_cast<VkBufferView*>(&resource->ViewHandle));
        VkAssert(result);
    }

    if (initial_data)
    {
        if (_resource_usage == resource_usage::CPU_ONLY)
        {
            setBufferInitialDataHostOnly(resource, num_data, initial_data, (uint64_t)alloc, _resource_usage);
        }
        else
        {
            setBufferInitialDataUploadBuffer(resource, num_data, initial_data, (uint64_t)alloc);
        }
    }

    return resource;
}

void ResourceContextImpl::setBufferData(VulkanResource* dest_buffer, const size_t num_data, const gpu_resource_data_t* data)
{
    resource_usage mem_type = resourceInfos.resourceMemoryType.at(dest_buffer);
    if (mem_type == resource_usage::CPU_ONLY)
    {
        setBufferInitialDataHostOnly(dest_buffer, num_data, data, resourceAllocations.at(dest_buffer), mem_type);
    }
    else
    {
        setBufferInitialDataUploadBuffer(dest_buffer, num_data, data, resourceAllocations.at(dest_buffer));
    }
}

VulkanResource* ResourceContextImpl::createImage(const VkImageCreateInfo* info, const VkImageViewCreateInfo* view_info, const size_t num_data, const gpu_image_resource_data_t* initial_data, const resource_usage _resource_usage, const resource_creation_flags _flags, void* user_data)
{
    VulkanResource* resource{ nullptr };
    VmaAllocationInfo* alloc_info{ nullptr };
    VmaAllocation alloc{ VK_NULL_HANDLE };
    VkImageViewCreateInfo* local_view_info{ nullptr };

    {
        // try to do all of our container modification up front, then work with references and pointers to emplaced data from there
        // QUESTION: Could this change anyways??? I think it can! Iterator invalidation is likely, and that's representative of the data moving in memory
        rw_lock_guard emplace_guard(lock_mode::Write, containerMutex);
        auto iter = resources.emplace(std::make_unique<VulkanResource>());
        resource = iter.first->get();
        auto info_iter = resourceInfos.imageInfos.emplace(resource, *info);
        resource->Type = resource_type::IMAGE;
        resource->Info = &info_iter.first->second;
        resource->UserData = user_data;
        resourceInfos.resourceMemoryType.emplace(resource, _resource_usage);
        auto alloc_info_iter = allocInfos.emplace(resource, std::make_unique<VmaAllocationInfo>());
        alloc_info = alloc_info_iter.first->second.get();
        auto alloc_iter = resourceAllocations.emplace(resource, VK_NULL_HANDLE);
        alloc = reinterpret_cast<VmaAllocation>(alloc_iter.first->second);
        if (view_info)
        {
            auto view_iter = resourceInfos.imageViewInfos.emplace(resource, *view_info);
            resource->ViewInfo = &view_iter.first->second;
            local_view_info = reinterpret_cast<VkImageViewCreateInfo*>(resource->ViewInfo);
        }
    }

    // This probably isn't ideal but it's a reasonable assumption to make. (updated to check for GPU only allocs)
    VkImageCreateInfo* create_info = reinterpret_cast<VkImageCreateInfo*>(resource->Info);
    if (!(create_info->usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) || (_resource_usage != resource_usage::GPU_ONLY))
    {
        create_info->usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    }

    VmaAllocationCreateInfo alloc_create_info
    {
        (VmaAllocationCreateFlags)_flags,
        (VmaMemoryUsage)_resource_usage,
        GetMemoryPropertyFlags(_resource_usage),
        0u,
        UINT32_MAX,
        VK_NULL_HANDLE,
        user_data
    };

    VkResult result = vmaCreateImage((VmaAllocator)allocatorHandle, create_info, &alloc_create_info, reinterpret_cast<VkImage*>(&resource->Handle), &alloc, alloc_info);
    VkAssert(result);

    if (view_info)
    {
        local_view_info->image = (VkImage)resource->Handle;
        result = vkCreateImageView(device->vkHandle(), local_view_info, nullptr, reinterpret_cast<VkImageView*>(&resource->ViewHandle));
        VkAssert(result);
    }

    if (initial_data)
    {
        setImageInitialData(resource, num_data, initial_data, (uint64_t)alloc);
    }

    return resource;
}

VulkanResource* ResourceContextImpl::createImageView(const VulkanResource* base_rsrc, const VkImageViewCreateInfo* view_info, void* user_data)
{
    decltype(resources)::const_iterator iter;
    {
        rw_lock_guard lock_guard(lock_mode::Read, containerMutex);
        auto iter = std::find_if(std::cbegin(resources), std::cend(resources), [base_rsrc](const std::unique_ptr<VulkanResource>& rsrc)
        {
            return base_rsrc == rsrc.get();
        });
    }

    if (iter != std::cend(resources))
    {
        VulkanResource* found_resource = iter->get();
        VulkanResource* result = nullptr;

        {
            rw_lock_guard modify_guard(lock_mode::Write, containerMutex);
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
    else
    {
        return nullptr;
    }
}

void ResourceContextImpl::setBufferInitialDataHostOnly(VulkanResource* resource, const size_t num_data, const gpu_resource_data_t* initial_data, uint64_t alloc, resource_usage _resource_usage)
{
    void* mapped_address{ nullptr };
    const VmaAllocationInfo* alloc_info;
    {
        rw_lock_guard lock_guard(lock_mode::Read, containerMutex);
        alloc_info = allocInfos.at(alloc).get();
    }
    VmaAllocator allocator = (VmaAllocator)allocatorHandle;
    VmaAllocation allocation = (VmaAllocation)alloc;

    VkResult result = vmaMapMemory(allocator, allocation, &mapped_address);
    VkAssert(result);
    size_t offset = 0u;
    for (size_t i = 0u; i < num_data; ++i)
    {
        void* curr_address = (void*)((size_t)mapped_address + offset);
        memcpy(curr_address, initial_data[i].Data, initial_data[i].DataSize);
        offset += initial_data[i].DataSize;
    }
    vmaUnmapMemory(allocator, allocation);
}

void ResourceContextImpl::setBufferInitialDataUploadBuffer(VulkanResource* resource, const size_t num_data, const gpu_resource_data_t* initial_data, uint64_t alloc)
{

    /*
        Set everything up we need for recording the command ahead of time, before acquiring the transfer system lock.
        Then create a buffer, acquire the spinlock and populate buffer + record transfer commands, release spinlock.
    */

    const VkBufferCreateInfo* p_info = reinterpret_cast<VkBufferCreateInfo*>(resource->Info);
    const VkBufferMemoryBarrier memory_barrier0
    {
        VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        nullptr,
        0u,
        VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_QUEUE_FAMILY_IGNORED,
        VK_QUEUE_FAMILY_IGNORED,
        reinterpret_cast<VkBuffer>(resource->Handle),
        0u,
        p_info->size
    };
    const VkBufferMemoryBarrier memory_barrier1
    {
        VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        nullptr,
        VK_ACCESS_TRANSFER_WRITE_BIT,
        accessFlagsFromBufferUsage(p_info->usage),
        VK_QUEUE_FAMILY_IGNORED,
        VK_QUEUE_FAMILY_IGNORED,
        (VkBuffer)resource->Handle,
        0u,
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

        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0u, 0u, 0u, 1u, &memory_barrier0, 0u, nullptr);
        vkCmdCopyBuffer(cmd, upload_buffer->Buffer, reinterpret_cast<VkBuffer>(resource->Handle), static_cast<uint32_t>(buffer_copies.size()), buffer_copies.data());
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0u, 0u, nullptr, 1u, &memory_barrier1, 0u, nullptr);
    }
}

void ResourceContextImpl::setImageInitialData(VulkanResource* resource, const size_t num_data, const gpu_image_resource_data_t* initial_data, uint64_t& alloc)
{
    const VkImageCreateInfo* info = reinterpret_cast<VkImageCreateInfo*>(resource->Info);

    const VkImageMemoryBarrier barrier0
    {
        VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        nullptr,
        0u,
        VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        device->QueueFamilyIndices().Transfer,
        device->QueueFamilyIndices().Transfer,
        reinterpret_cast<VkImage>(resource->Handle),
        VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0u, info->mipLevels, 0u, info->arrayLayers }
    };

    const VkImageMemoryBarrier barrier1
    {
            VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            nullptr,
            VK_ACCESS_TRANSFER_WRITE_BIT,
            accessFlagsFromImageUsage(info->usage),
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            imageLayoutFromUsage(info->usage),
            device->QueueFamilyIndices().Transfer,
            device->QueueFamilyIndices().Graphics,
            reinterpret_cast<VkImage>(resource->Handle),
            VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0u, info->mipLevels, 0u, info->arrayLayers }
    };


    auto& transfer_system = ResourceTransferSystem::GetTransferSystem();
    {
        auto guard = transfer_system.AcquireSpinLock();
        UploadBuffer* upload_buffer = transfer_system.CreateUploadBuffer(alloc.Size);
        VkCommandBuffer cmd = transfer_system.TransferCmdBuffer();
        std::vector<VkBufferImageCopy> buffer_image_copies;
        size_t copy_offset = 0u;
        for (uint32_t i = 0u; i < num_data; ++i)
        {
            buffer_image_copies.emplace_back(VkBufferImageCopy{
                copy_offset,
                0u,
                0u,
                VkImageSubresourceLayers{ VK_IMAGE_ASPECT_COLOR_BIT, initial_data[i].MipLevel, initial_data[i].ArrayLayer, initial_data[i].NumLayers },
                VkOffset3D{ 0, 0, 0 },
                VkExtent3D{ initial_data[i].Width, initial_data[i].Height, 1u }
                });
            assert(initial_data[i].MipLevel < info->mipLevels);
            assert(initial_data[i].ArrayLayer < info->arrayLayers);
            upload_buffer->SetData(initial_data[i].Data, initial_data[i].DataSize, copy_offset);
            copy_offset += initial_data[i].DataSize;
        }
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0u, 0u, nullptr, 0u, nullptr, 1u, &barrier0);
        vkCmdCopyBufferToImage(cmd, upload_buffer->Buffer, reinterpret_cast<VkImage>(resource->Handle), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, static_cast<uint32_t>(buffer_image_copies.size()), buffer_image_copies.data());
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0u, 0u, nullptr, 0u, nullptr, 1u, &barrier1);
    }

}

VkFormatFeatureFlags ResourceContextImpl::featureFlagsFromUsage(const VkImageUsageFlags flags) const noexcept {
    VkFormatFeatureFlags result = 0;
    if (flags & VK_IMAGE_USAGE_SAMPLED_BIT)
    {
        result |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    }
    if (flags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
    {
        result |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
    }
    if (flags & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
    {
        result |= VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
    }
    if (flags & VK_IMAGE_USAGE_STORAGE_BIT)
    {
        result |= VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
    }
    return result;
}

void ResourceContextImpl::createBufferResourceCopy(VulkanResource * src, VulkanResource** dst)
{
    const VkBufferCreateInfo* create_info = reinterpret_cast<const VkBufferCreateInfo*>(src->Info);
    const VkBufferViewCreateInfo* view_info = nullptr;
    if (src->ViewHandle != VK_NULL_HANDLE)
    { 
        view_info = reinterpret_cast<const VkBufferViewCreateInfo*>(src->ViewInfo);
    }
    *dst = CreateBuffer(create_info, view_info, 0, nullptr, resourceInfos.resourceMemoryType.at(src), nullptr);
    CopyResourceContents(src, *dst);
}

void ResourceContextImpl::createImageResourceCopy(VulkanResource * src, VulkanResource** dst)
{
    const VkImageCreateInfo* image_info = reinterpret_cast<const VkImageCreateInfo*>(src->Info);
    const VkImageViewCreateInfo* view_info = nullptr;
    if (src->ViewHandle != VK_NULL_HANDLE)
    {
        view_info = reinterpret_cast<const VkImageViewCreateInfo*>(src->ViewInfo);
    }
    *dst = CreateImage(image_info, view_info, 0, nullptr, resourceInfos.resourceMemoryType.at(src), nullptr);
    CopyResourceContents(src, *dst);
}

void ResourceContextImpl::createSamplerResourceCopy(VulkanResource * src, VulkanResource** dst)
{
    const VkSamplerCreateInfo* sampler_info = reinterpret_cast<const VkSamplerCreateInfo*>(src->Info);
    *dst = CreateSampler(sampler_info, nullptr);
}

void ResourceContextImpl::createCombinedImageSamplerResourceCopy(VulkanResource* src, VulkanResource** dest)
{
    createImageResourceCopy(src, dest);
    VulkanResource** sampler_to_create = &(*dest)->Sampler;
    createSamplerResourceCopy(src->Sampler, sampler_to_create);
    (*dest)->Type = resource_type::COMBINED_IMAGE_SAMPLER;
}

void ResourceContextImpl::copyBufferContentsToBuffer(VulkanResource* src, VulkanResource* dst)
{

    const VkBufferCreateInfo* src_info = reinterpret_cast<const VkBufferCreateInfo*>(src->Info);
    const VkBufferCreateInfo* dst_info = reinterpret_cast<const VkBufferCreateInfo*>(dst->Info);
    assert(src_info->size <= dst_info->size);
    const VkBufferCopy copy
    {
        0u,
        0u,
        src_info->size
    };

    const VkBufferMemoryBarrier pre_transfer_barriers[2] 
    {
        VkBufferMemoryBarrier
        {
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
        VkBufferMemoryBarrier
        {
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

    const VkBufferMemoryBarrier post_transfer_barriers[2]
    {
        VkBufferMemoryBarrier
        {
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
        VkBufferMemoryBarrier
        {
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

        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0u, 0u, nullptr, 2u, pre_transfer_barriers, 0u, nullptr);
        vkCmdCopyBuffer(cmd, (VkBuffer)src->Handle, (VkBuffer)dst->Handle, 1, &copy);
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0u, 0u, nullptr, 2u, post_transfer_barriers, 0u, nullptr);
    }
}

void ResourceContextImpl::copyImageContentsToImage(VulkanResource* src, VulkanResource* dst)
{
    throw std::runtime_error("Not yet implemented!");
}

void ResourceContextImpl::copyBufferContentsToImage(VulkanResource* src, VulkanResource* dst)
{
    throw std::runtime_error("Not yet implemented!");
}

void ResourceContextImpl::copyImageContentsToBuffer(VulkanResource* src, VulkanResource* dst)
{
    throw std::runtime_error("Not yet implemented!");
}

void ResourceContextImpl::destroyResource(resource_iter_t iter)
{
    switch (iter->get()->Type) 
    {
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

void ResourceContextImpl::destroyBuffer(resource_iter_t iter)
{
    VulkanResource* rsrc = iter->get();
    if (rsrc->ViewHandle != 0)
    {
        vkDestroyBufferView(device->vkHandle(), (VkBufferView)rsrc->ViewHandle, nullptr);
    }
    vkDestroyBuffer(device->vkHandle(), (VkBuffer)rsrc->Handle, nullptr);
    allocator->FreeMemory(&resourceAllocations.at(rsrc)); 
    resources.erase(iter);
    resourceInfos.bufferInfos.erase(rsrc);
    resourceInfos.bufferViewInfos.erase(rsrc);
    resourceAllocations.erase(rsrc);
}

void ResourceContextImpl::destroyImage(resource_iter_t iter)
{
    VulkanResource* rsrc = iter->get();
    if (rsrc->ViewHandle != 0)
    {
        vkDestroyImageView(device->vkHandle(), (VkImageView)rsrc->ViewHandle, nullptr);
    }
    vkDestroyImage(device->vkHandle(), (VkImage)rsrc->Handle, nullptr);
    allocator->FreeMemory(&resourceAllocations.at(rsrc));
    resources.erase(iter);
    resourceInfos.imageInfos.erase(rsrc);
    resourceInfos.imageViewInfos.erase(rsrc);
    resourceAllocations.erase(rsrc);
}

void ResourceContextImpl::destroySampler(resource_iter_t iter)
{
    VulkanResource* rsrc = iter->get();
    vkDestroySampler(device->vkHandle(), (VkSampler)rsrc->Handle, nullptr);
    resourceInfos.samplerInfos.erase(rsrc);
    resources.erase(iter);
}
