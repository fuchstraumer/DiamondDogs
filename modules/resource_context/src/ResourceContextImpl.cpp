#include "ResourceContextImpl.hpp"
#include "../../rendering_context/include/RenderingContext.hpp"
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>
#include <fstream>
#define THSVS_SIMPLER_VULKAN_SYNCHRONIZATION_IMPLEMENTATION
#include <thsvs_simpler_vulkan_synchronization.h>

static std::vector<ThsvsAccessType> thsvsAccessTypesFromBufferUsage(VkBufferUsageFlagBits _flags)
{
    std::vector<ThsvsAccessType> results;
    if (_flags & VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)
    {
        results.emplace_back(THSVS_ACCESS_ANY_SHADER_READ_UNIFORM_BUFFER);
    }
    if (_flags & VK_BUFFER_USAGE_INDEX_BUFFER_BIT)
    {
        results.emplace_back(THSVS_ACCESS_INDEX_BUFFER);
    }
    if (_flags & VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)
    {
        results.emplace_back(THSVS_ACCESS_VERTEX_BUFFER);
    }
    if (_flags & VK_BUFFER_USAGE_INDEX_BUFFER_BIT)
    {
        results.emplace_back(THSVS_ACCESS_INDIRECT_BUFFER);
    }
    if (_flags & VK_BUFFER_USAGE_CONDITIONAL_RENDERING_BIT_EXT)
    {
        results.emplace_back(THSVS_ACCESS_CONDITIONAL_RENDERING_READ_EXT);
    }
    if (_flags & VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT)
    {
        results.emplace_back(THSVS_ACCESS_ANY_SHADER_READ_SAMPLED_IMAGE_OR_UNIFORM_TEXEL_BUFFER);
    }
    if ((_flags & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) || (_flags & VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT))
    {
        results.emplace_back(THSVS_ACCESS_ANY_SHADER_WRITE);
    }
    if (_flags & VK_BUFFER_USAGE_TRANSFER_DST_BIT)
    {
        results.emplace_back(THSVS_ACCESS_TRANSFER_WRITE);
    }
    if (_flags & VK_BUFFER_USAGE_TRANSFER_SRC_BIT)
    {
        results.emplace_back(THSVS_ACCESS_TRANSFER_READ);
    }

    if (results.empty())
    {
        // Didn't match any flags. Go super general.
        results.emplace_back(THSVS_ACCESS_ANY_SHADER_WRITE);
        results.emplace_back(THSVS_ACCESS_ANY_SHADER_READ_OTHER);
    }

    return results;
}

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

static std::vector<ThsvsAccessType> thsvsAccessTypesFromImageUsage(VkImageUsageFlags _flags)
{
    std::vector<ThsvsAccessType> results;

    if (_flags & VK_IMAGE_USAGE_SAMPLED_BIT)
    {
        results.emplace_back(THSVS_ACCESS_ANY_SHADER_READ_SAMPLED_IMAGE_OR_UNIFORM_TEXEL_BUFFER);
    }
    if (_flags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
    {
        results.emplace_back(THSVS_ACCESS_COLOR_ATTACHMENT_READ_WRITE);
    }
    if (_flags & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
    {
        results.emplace_back(THSVS_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ);
        results.emplace_back(THSVS_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE);
    }
    if (_flags & VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT)
    {
        results.emplace_back(THSVS_ACCESS_COLOR_ATTACHMENT_READ);
    }
    if (_flags & VK_IMAGE_USAGE_SHADING_RATE_IMAGE_BIT_NV)
    {
        results.emplace_back(THSVS_ACCESS_SHADING_RATE_READ_NV);
    }
    if (_flags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
    {
        results.emplace_back(THSVS_ACCESS_TRANSFER_READ);
    }
    /*
    if (_flags & VK_IMAGE_USAGE_TRANSFER_DST_BIT)
    {
        results.emplace_back(THSVS_ACCESS_TRANSFER_WRITE);
    }
    */
    if (_flags & VK_IMAGE_USAGE_FRAGMENT_DENSITY_MAP_BIT_EXT)
    {
        results.emplace_back(THSVS_ACCESS_FRAGMENT_DENSITY_MAP_READ_EXT);
    }

    if (results.empty() || (_flags & VK_IMAGE_USAGE_STORAGE_BIT))
    {
        results.emplace_back(THSVS_ACCESS_ANY_SHADER_READ_OTHER);
        results.emplace_back(THSVS_ACCESS_ANY_SHADER_WRITE);
    }

    return results;
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

static vpr::Allocator::allocation_extensions getExtensionFlags(const vpr::Device* device)
{
    return device->DedicatedAllocationExtensionsEnabled() ? vpr::Allocator::allocation_extensions::DedicatedAllocations : vpr::Allocator::allocation_extensions::None;
}

enum class lock_mode : uint8_t
{
    Invalid = 0,
    Read = 1,
    Write = 2
};

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


void ResourceContextImpl::construct(vpr::Device* _device, vpr::PhysicalDevice* physical_device, bool validation_enabled)
{
    device = _device;
	validationEnabled = validation_enabled;
	if (validationEnabled)
	{
		vkDebugFns = device->DebugUtilsHandler();
	}

    auto flags = getExtensionFlags(device);

    VmaAllocatorCreateInfo create_info
    {
        flags == vpr::Allocator::allocation_extensions::DedicatedAllocations ? VMA_ALLOCATOR_CREATE_KHR_DEDICATED_ALLOCATION_BIT : 0u,
        physical_device->vkHandle(),
        device->vkHandle(),
		VkDeviceSize(6.4e+7),
        nullptr,
        nullptr,
        1u,
        nullptr,
        nullptr,
        nullptr
    };

    VkResult result = vmaCreateAllocator(&create_info, &allocatorHandle);
    VkAssert(result);

    auto& transfer_system = ResourceTransferSystem::GetTransferSystem();
    transfer_system.Initialize(_device, allocatorHandle);

	reserveSpaceInContainers(4096u); // pretty heavily over-reserving but it's safer than the alternative
}

void ResourceContextImpl::destroy()
{
	for (auto& resource : resources)
	{
		destroyResource(resource.get());
	}

	resources.clear();
	resourceAllocations.clear();
	resourceNames.clear();
	imageViews.clear();
	allocInfos.clear();
	resourceInfos.clear();
}

void ResourceContextImpl::update()
{
    auto prevMinusOneSize = prevContainerMaxSize;
    prevContainerMaxSize = resourceAllocations.size();
    size_t delta = prevContainerMaxSize - prevMinusOneSize;
    // set delta to 128u if it doesn't change, as a nice safe container (and if it still needs to rehash, it's just reserving some extra headroom)
    maxContainerDelta = delta > 0 ? delta > maxContainerDelta ? delta : maxContainerDelta : 128u;
    if (rehashContainers())
    {
		reserveSpaceInContainers(resourceAllocations.size() * 2u);
    }

	auto& transfer_system = ResourceTransferSystem::GetTransferSystem();
	transfer_system.CompleteTransfers();
}

void ResourceContextImpl::destroyResource(VulkanResource* rsrc)
{
    decltype(resources)::const_iterator iter;
    {
        rw_lock_guard erase_guard(lock_mode::Read, containerMutex); // only need read to find it
        iter = std::find_if(std::begin(resources), std::end(resources), [rsrc](const decltype(resources)::value_type& entry) {
            return entry.get() == rsrc;
        });

        if (iter == std::end(resources))
        {
            LOG(ERROR) << "Tried to erase resource that isn't in internal containers!";
            throw std::runtime_error("Tried to erase resource that isn't in internal containers!");
        }

    }

    std::unique_lock destroy_lock(containerMutex);
    // for samplers, iterator becomes invalid after erased (and ordering was req'd for threading)
    VulkanResource* samplerResource = iter->get()->Sampler;
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
        destroyImage(iter);
        destroy_lock.unlock(); // gotta release it now so that the recursive call works
        assert(samplerResource);
        destroyResource(samplerResource);
        break;
    case resource_type::INVALID:
        [[fallthrough]];
    default:
        throw std::runtime_error("Invalid resource type!");
    }

    rsrc = nullptr;
}

void* ResourceContextImpl::map(VulkanResource* resource, size_t size, size_t offset)
{
    resource_usage alloc_usage{ resource_usage::INVALID_RESOURCE_USAGE };
    VmaAllocation alloc{ VK_NULL_HANDLE };

    {
        rw_lock_guard lock_guard(lock_mode::Read, containerMutex);
        alloc = resourceAllocations.at(resource);
        alloc_usage = resourceInfos.resourceMemoryType.at(resource);
        if (alloc_usage == resource_usage::GPU_TO_CPU)
        {
            vmaInvalidateAllocation(allocatorHandle, alloc, offset, size == 0u ? VK_WHOLE_SIZE : size);
        }
    }

    void* mapped_ptr = nullptr;
    VkResult result = vmaMapMemory(allocatorHandle, alloc, &mapped_ptr);
    VkAssert(result);
    return mapped_ptr;
}

void ResourceContextImpl::unmap(VulkanResource* resource, size_t size, size_t offset)
{
    VmaAllocation alloc{ VK_NULL_HANDLE };
    resource_usage alloc_usage{ resource_usage::INVALID_RESOURCE_USAGE };

    {
        rw_lock_guard lock_guard(lock_mode::Read, containerMutex);
        alloc = resourceAllocations.at(resource);
        alloc_usage = resourceInfos.resourceMemoryType.at(resource);
    }

    vmaUnmapMemory(allocatorHandle, alloc);
    if (alloc_usage == resource_usage::CPU_TO_GPU)
    {
        vmaFlushAllocation(allocatorHandle, alloc, offset, size == 0u ? VK_WHOLE_SIZE : size);
    }
}

VulkanResource* ResourceContextImpl::createBuffer(const VkBufferCreateInfo* info, const VkBufferViewCreateInfo* view_info, const size_t num_data, const gpu_resource_data_t* initial_data, const resource_usage _resource_usage, const resource_creation_flags _flags, void* user_data)
{
    VulkanResource* resource{ nullptr };
    VmaAllocationInfo* alloc_info{ nullptr };
    VmaAllocation* alloc{ nullptr };
    VkBufferViewCreateInfo* local_view_info{ nullptr };

    {
        // write to containers now then just read from em for the rest
        rw_lock_guard emplace_guard(lock_mode::Write, containerMutex);
        auto iter = resources.emplace(std::make_unique<VulkanResource>());
        resource = iter.first->get();
        auto info_iter = resourceInfos.bufferInfos.emplace(resource, *info);
        resource->Type = resource_type::BUFFER;
        resource->Info = &info_iter.first->second;
        resource->UserData = user_data;
        auto alloc_info_iter = allocInfos.emplace(resource, VmaAllocationInfo());
        alloc_info = &alloc_info_iter.first->second;
        resourceInfos.resourceMemoryType.emplace(resource, _resource_usage);
        auto alloc_iter = resourceAllocations.emplace(resource, VmaAllocation());
        alloc = &alloc_iter.first->second;
        if (view_info)
        {
            auto view_iter = resourceInfos.bufferViewInfos.emplace(resource, *view_info);
            resource->ViewInfo = &view_iter.first->second;
            local_view_info = reinterpret_cast<VkBufferViewCreateInfo*>(resource->ViewInfo);
        }
		resourceInfos.resourceFlags.emplace(resource, _flags);
    }

    if ((_resource_usage == resource_usage::CPU_TO_GPU || _resource_usage == resource_usage::GPU_TO_CPU || _resource_usage == resource_usage::GPU_ONLY) && (num_data != 0))
    {
        // Device local buffer that will be transferred into, make sure it has the requisite flag.
        VkBufferCreateInfo* buffer_info = reinterpret_cast<VkBufferCreateInfo*>(resource->Info);
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

    VkResult result = vmaCreateBuffer(allocatorHandle, reinterpret_cast<VkBufferCreateInfo*>(resource->Info), &alloc_create_info,
        reinterpret_cast<VkBuffer*>(&resource->Handle), alloc, alloc_info);
    VkAssert(result);

	if constexpr (VTF_USE_DEBUG_INFO && VTF_VALIDATION_ENABLED)
	{
		if (_flags & ResourceCreateUserDataAsString)
		{
			const std::string object_name{ reinterpret_cast<const char*>(user_data) };
			result = RenderingContext::SetObjectName(VK_OBJECT_TYPE_BUFFER, resource->Handle, VTF_DEBUG_OBJECT_NAME(object_name.c_str()));
			VkAssert(result);
		}
	}

    if (view_info)
    {
        local_view_info->buffer = (VkBuffer)resource->Handle;
        result = vkCreateBufferView(device->vkHandle(), local_view_info, nullptr, reinterpret_cast<VkBufferView*>(&resource->ViewHandle));
        VkAssert(result);
		if constexpr (VTF_USE_DEBUG_INFO && VTF_VALIDATION_ENABLED)
		{
			if (_flags & ResourceCreateUserDataAsString)
			{
				const std::string object_view_name = std::string(reinterpret_cast<const char*>(user_data)) + std::string("_view");
				result = RenderingContext::SetObjectName(VK_OBJECT_TYPE_BUFFER_VIEW, resource->ViewHandle, VTF_DEBUG_OBJECT_NAME(object_view_name.c_str()));
				VkAssert(result);
			}
		}
    }

    if (initial_data)
    {
        if (_resource_usage == resource_usage::CPU_ONLY)
        {
            setBufferInitialDataHostOnly(resource, num_data, initial_data, _resource_usage);
        }
        else
        {
            setBufferInitialDataUploadBuffer(resource, num_data, initial_data);
        }
    }

    return resource;
}

void ResourceContextImpl::setBufferData(VulkanResource* dest_buffer, const size_t num_data, const gpu_resource_data_t* data)
{
    resource_usage mem_type = resourceInfos.resourceMemoryType.at(dest_buffer);
    if (mem_type == resource_usage::CPU_ONLY)
    {
        setBufferInitialDataHostOnly(dest_buffer, num_data, data, mem_type);
    }
    else
    {
        setBufferInitialDataUploadBuffer(dest_buffer, num_data, data);
    }
}

VulkanResource* ResourceContextImpl::createImage(const VkImageCreateInfo* info, const VkImageViewCreateInfo* view_info, const size_t num_data, const gpu_image_resource_data_t* initial_data, const resource_usage _resource_usage, const resource_creation_flags _flags, void* user_data)
{
    VulkanResource* resource{ nullptr };
    VmaAllocationInfo* alloc_info{ nullptr };
    VmaAllocation* alloc{};
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
        auto alloc_info_iter = allocInfos.emplace(resource, VmaAllocationInfo());
        alloc_info = &alloc_info_iter.first->second;
        auto alloc_iter = resourceAllocations.emplace(resource, VmaAllocation());
		if (!alloc_iter.second)
		{
			throw std::runtime_error("Failed to emplace allocation!");
		}
        alloc = &alloc_iter.first->second;
        if (view_info)
        {
            auto view_iter = resourceInfos.imageViewInfos.emplace(resource, *view_info);
            resource->ViewInfo = &view_iter.first->second;
            local_view_info = reinterpret_cast<VkImageViewCreateInfo*>(resource->ViewInfo);
        }
		resourceInfos.resourceFlags.emplace(resource, _flags);
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

    VkResult result = vmaCreateImage((VmaAllocator)allocatorHandle, create_info, &alloc_create_info, reinterpret_cast<VkImage*>(&resource->Handle), alloc, alloc_info);
    VkAssert(result);
	if (_flags & ResourceCreateUserDataAsString)
	{
		result = RenderingContext::SetObjectName(VK_OBJECT_TYPE_IMAGE, resource->Handle, VTF_DEBUG_OBJECT_NAME(reinterpret_cast<const char*>(user_data)));
		VkAssert(result);
	}
	

    if (view_info)
    {
        local_view_info->image = (VkImage)resource->Handle;
        result = vkCreateImageView(device->vkHandle(), local_view_info, nullptr, reinterpret_cast<VkImageView*>(&resource->ViewHandle));
        VkAssert(result);
		if (_flags & ResourceCreateUserDataAsString)
		{
			const std::string view_name_string = std::string(reinterpret_cast<const char*>(user_data)) + std::string("_view");
			result = RenderingContext::SetObjectName(VK_OBJECT_TYPE_IMAGE_VIEW, resource->ViewHandle, VTF_DEBUG_OBJECT_NAME(view_name_string.c_str()));
			VkAssert(result);
		}
    }

    if (initial_data)
    {
        setImageInitialData(resource, num_data, initial_data);
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

VulkanResource* ResourceContextImpl::createSampler(const VkSamplerCreateInfo* info, const resource_creation_flags _flags, void* user_data)
{
    VulkanResource* resource = nullptr;
    VkSamplerCreateInfo* local_info{ nullptr };

    {
        rw_lock_guard emplace_guard(lock_mode::Write, containerMutex);
        auto iter = resources.emplace(std::make_unique<VulkanResource>());
        assert(iter.second);
        resource = iter.first->get();
        auto info_iter = resourceInfos.samplerInfos.emplace(resource, *info);
        assert(info_iter.second);
        local_info = &info_iter.first->second;
		resourceInfos.resourceFlags.emplace(resource, _flags);
    }

    resource->Type = resource_type::SAMPLER;
    resource->Info = local_info;
    resource->UserData = user_data;

    VkResult result = vkCreateSampler(device->vkHandle(), info, nullptr, reinterpret_cast<VkSampler*>(&resource->Handle));
    VkAssert(result);

	if (_flags & ResourceCreateUserDataAsString)
	{
		result = RenderingContext::SetObjectName(VK_OBJECT_TYPE_SAMPLER, resource->Handle, VTF_DEBUG_OBJECT_NAME(reinterpret_cast<const char*>(user_data)));
		VkAssert(result);
	}

    return resource;
}

void ResourceContextImpl::copyResourceContents(VulkanResource* src, VulkanResource* dst)
{
	throw std::runtime_error("Not implemented!");
}

void ResourceContextImpl::setBufferInitialDataHostOnly(VulkanResource* resource, const size_t num_data, const gpu_resource_data_t* initial_data, resource_usage _resource_usage)
{
    void* mapped_address{ nullptr };
    const VmaAllocationInfo* alloc_info;
	VmaAllocation* alloc;
    {
        rw_lock_guard lock_guard(lock_mode::Read, containerMutex);
        alloc_info = &allocInfos.at(resource);
		alloc = &resourceAllocations.at(resource);
    }

	VkResult result = vmaMapMemory(allocatorHandle, *alloc, &mapped_address);
    VkAssert(result);
    size_t offset = 0u;
    for (size_t i = 0u; i < num_data; ++i)
    {
        void* curr_address = (void*)((size_t)mapped_address + offset);
        memcpy(curr_address, initial_data[i].Data, initial_data[i].DataSize);
        offset += initial_data[i].DataSize;
    }
    vmaUnmapMemory(allocatorHandle, *alloc);
    vmaFlushAllocation(allocatorHandle, *alloc, 0u, offset);
}

void ResourceContextImpl::setBufferInitialDataUploadBuffer(VulkanResource* resource, const size_t num_data, const gpu_resource_data_t* initial_data)
{

    /*
        Set everything up we need for recording the command ahead of time, before acquiring the transfer system lock.
        Then create a buffer, acquire the spinlock and populate buffer + record transfer commands, release spinlock.
    */

    const VkBufferCreateInfo* p_info = reinterpret_cast<VkBufferCreateInfo*>(resource->Info);
	const uint32_t transfer_queue_idx = device->QueueFamilyIndices().Transfer;

    const ThsvsAccessType transfer_access_types[1]{
        THSVS_ACCESS_TRANSFER_WRITE
    };

    std::vector<ThsvsAccessType> possible_access_types = thsvsAccessTypesFromBufferUsage((VkBufferUsageFlagBits)p_info->usage);

    ThsvsBufferBarrier post_transfer_barrier{
        1u,
        transfer_access_types,
        static_cast<uint32_t>(possible_access_types.size()),
        possible_access_types.data(),
        VK_QUEUE_FAMILY_IGNORED,
        VK_QUEUE_FAMILY_IGNORED,
        (VkBuffer)resource->Handle,
        0u,
        p_info->size
    };

	if (p_info->sharingMode == VK_SHARING_MODE_EXCLUSIVE)
	{
		// update for proper ownership transfer
        assert(initial_data->DestinationQueueFamily != VK_QUEUE_FAMILY_IGNORED);
        assert(transfer_queue_idx != VK_QUEUE_FAMILY_IGNORED);
		post_transfer_barrier.srcQueueFamilyIndex = initial_data->DestinationQueueFamily != transfer_queue_idx ? transfer_queue_idx : VK_QUEUE_FAMILY_IGNORED;
		post_transfer_barrier.dstQueueFamilyIndex = initial_data->DestinationQueueFamily != transfer_queue_idx ? initial_data->DestinationQueueFamily : VK_QUEUE_FAMILY_IGNORED;
	}

    const ThsvsGlobalBarrier global_barrier{
        1u,
        transfer_access_types,
        static_cast<uint32_t>(possible_access_types.size()),
        possible_access_types.data()
    };

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

    vkCmdCopyBuffer(cmd, upload_buffer->Buffer, reinterpret_cast<VkBuffer>(resource->Handle), static_cast<uint32_t>(buffer_copies.size()), buffer_copies.data());
    thsvsCmdPipelineBarrier(cmd, &global_barrier, 1u, &post_transfer_barrier, 0u, nullptr);
    
}

void ResourceContextImpl::setImageInitialData(VulkanResource* resource, const size_t num_data, const gpu_image_resource_data_t* initial_data)
{
    const VkImageCreateInfo* info = reinterpret_cast<VkImageCreateInfo*>(resource->Info);

    constexpr static ThsvsAccessType transfer_access_types[1]{
        THSVS_ACCESS_TRANSFER_WRITE
    };

    // required, unlike with buffers. forces a layout transition.
    const ThsvsImageBarrier pre_transfer_barrier
    {
        0u,
        nullptr,
        1u,
        transfer_access_types,
        THSVS_IMAGE_LAYOUT_GENERAL,
        THSVS_IMAGE_LAYOUT_OPTIMAL,
        VK_FALSE,
        VK_QUEUE_FAMILY_IGNORED,
        VK_QUEUE_FAMILY_IGNORED,
        (VkImage)resource->Handle,
        VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0u, info->mipLevels, 0u, info->arrayLayers }
    };


    auto post_transfer_access_types = thsvsAccessTypesFromImageUsage(info->usage);

    ThsvsImageBarrier post_transfer_barrier
    {
        1u,
        transfer_access_types,
        static_cast<uint32_t>(post_transfer_access_types.size()),
        post_transfer_access_types.data(),
        THSVS_IMAGE_LAYOUT_OPTIMAL,
        THSVS_IMAGE_LAYOUT_OPTIMAL,
        VK_FALSE,
        VK_QUEUE_FAMILY_IGNORED,
        VK_QUEUE_FAMILY_IGNORED,
        (VkImage)resource->Handle,
        VkImageSubresourceRange { VK_IMAGE_ASPECT_COLOR_BIT, 0u, info->mipLevels, 0u, info->arrayLayers }
    };

	const uint32_t transfer_queue_idx = device->QueueFamilyIndices().Transfer;

    if (info->sharingMode == VK_SHARING_MODE_EXCLUSIVE)
    {
        assert(initial_data->DestinationQueueFamily != VK_QUEUE_FAMILY_IGNORED);
        assert(transfer_queue_idx != VK_QUEUE_FAMILY_IGNORED);
        post_transfer_barrier.srcQueueFamilyIndex = transfer_queue_idx != initial_data->DestinationQueueFamily ? transfer_queue_idx : VK_QUEUE_FAMILY_IGNORED;
        post_transfer_barrier.dstQueueFamilyIndex = transfer_queue_idx != initial_data->DestinationQueueFamily ? initial_data->DestinationQueueFamily : VK_QUEUE_FAMILY_IGNORED;
    }

    auto& transfer_system = ResourceTransferSystem::GetTransferSystem();
    {
        auto guard = transfer_system.AcquireSpinLock();
		VmaAllocation& alloc = resourceAllocations.at(resource);
        UploadBuffer* upload_buffer = transfer_system.CreateUploadBuffer(alloc->GetSize());
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

        thsvsCmdPipelineBarrier(cmd, nullptr, 0u, nullptr, 1u, &pre_transfer_barrier);
        vkCmdCopyBufferToImage(cmd, upload_buffer->Buffer, reinterpret_cast<VkImage>(resource->Handle), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, static_cast<uint32_t>(buffer_image_copies.size()), buffer_image_copies.data());
        thsvsCmdPipelineBarrier(cmd, nullptr, 0u, nullptr, 1u, &post_transfer_barrier);
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

void ResourceContextImpl::writeStatsJsonFile(const char* output_file)
{
	char* output;
	vmaBuildStatsString(allocatorHandle, &output, VK_TRUE);
	std::ofstream outputFile(output_file);
	if (!outputFile.is_open())
	{
		throw std::runtime_error("Failed to open output file for JSON dump!");
	}

	outputFile.write(output, strlen(output));
	vmaFreeStatsString(allocatorHandle, output);
}

void ResourceContextImpl::createBufferResourceCopy(VulkanResource * src, VulkanResource** dst)
{
    const VkBufferCreateInfo* create_info = reinterpret_cast<const VkBufferCreateInfo*>(src->Info);
    const VkBufferViewCreateInfo* view_info = nullptr;
    if (src->ViewHandle != VK_NULL_HANDLE)
    {
        view_info = reinterpret_cast<const VkBufferViewCreateInfo*>(src->ViewInfo);
    }
    *dst = createBuffer(create_info, view_info, 0, nullptr, resourceInfos.resourceMemoryType.at(src), 0, nullptr);
}

void ResourceContextImpl::createImageResourceCopy(VulkanResource * src, VulkanResource** dst)
{
    const VkImageCreateInfo* image_info = reinterpret_cast<const VkImageCreateInfo*>(src->Info);
    const VkImageViewCreateInfo* view_info = nullptr;
    if (src->ViewHandle != VK_NULL_HANDLE)
    {
        view_info = reinterpret_cast<const VkImageViewCreateInfo*>(src->ViewInfo);
    }
    *dst = createImage(image_info, view_info, 0, nullptr, resourceInfos.resourceMemoryType.at(src), resourceInfos.resourceFlags.at(src), nullptr);
    copyResourceContents(src, *dst);
}

void ResourceContextImpl::createSamplerResourceCopy(VulkanResource * src, VulkanResource** dst)
{
    const VkSamplerCreateInfo* sampler_info = reinterpret_cast<const VkSamplerCreateInfo*>(src->Info);
    *dst = createSampler(sampler_info, resource_creation_flags(), nullptr);
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

void ResourceContextImpl::destroyBuffer(resource_iter_t iter)
{
    VulkanResource* rsrc = iter->get();
    if (rsrc->ViewHandle != 0)
    {
        vkDestroyBufferView(device->vkHandle(), (VkBufferView)rsrc->ViewHandle, nullptr);
    }
	vmaDestroyBuffer(allocatorHandle, (VkBuffer)rsrc->Handle, resourceAllocations.at(rsrc));
    resourceInfos.bufferInfos.erase(rsrc);
    resourceInfos.bufferViewInfos.erase(rsrc);
    resourceAllocations.erase(rsrc);
    resourceInfos.resourceMemoryType.erase(rsrc);
    resourceInfos.resourceFlags.erase(rsrc);
    resources.erase(iter);
}

void ResourceContextImpl::destroyImage(resource_iter_t iter)
{
    VulkanResource* rsrc = iter->get();
    if (rsrc->ViewHandle != 0)
    {
        vkDestroyImageView(device->vkHandle(), (VkImageView)rsrc->ViewHandle, nullptr);
    }
	vmaDestroyImage(allocatorHandle, (VkImage)rsrc->Handle, resourceAllocations.at(rsrc));
    resources.erase(iter);
    resourceInfos.imageInfos.erase(rsrc);
    resourceInfos.imageViewInfos.erase(rsrc);
    resourceAllocations.erase(rsrc);
    resourceInfos.resourceMemoryType.erase(rsrc);
    resourceInfos.resourceFlags.erase(rsrc);
}

void ResourceContextImpl::destroySampler(resource_iter_t iter)
{
    VulkanResource* rsrc = iter->get();
    vkDestroySampler(device->vkHandle(), (VkSampler)rsrc->Handle, nullptr);
    resourceInfos.samplerInfos.erase(rsrc);
    resources.erase(iter);
}

bool ResourceContextImpl::rehashContainers() noexcept
{
    rw_lock_guard read_guard(lock_mode::Read, containerMutex);
    const size_t headroom = maxContainerDelta; // based on per-frame checks of allocations size, as this will always be the most frequently modified
	size_t currLoad{ 0u };
	bool rehash = resourceInfos.mayNeedRehash(headroom);
    currLoad = static_cast<size_t>(std::floorf(resourceNames.max_load_factor())) * resourceNames.bucket_count();
    rehash |= (currLoad + headroom) > resourceNames.max_size();
    currLoad = static_cast<size_t>(std::floorf(resourceNames.max_load_factor())) * resourceAllocations.bucket_count();
    rehash |= (currLoad + headroom) > resourceAllocations.max_size();
    currLoad = static_cast<size_t>(std::floorf(resourceNames.max_load_factor())) * imageViews.bucket_count();
    rehash |= (currLoad + headroom) > imageViews.max_size();
    currLoad = static_cast<size_t>(std::floorf(resourceNames.max_load_factor())) * allocInfos.bucket_count();
    rehash |= (currLoad + headroom) > allocInfos.max_size();
    currLoad = static_cast<size_t>(std::floorf(resourceNames.max_load_factor())) * resources.bucket_count();
    rehash |= (currLoad + headroom) > resources.max_size();
    return rehash;
}

void ResourceContextImpl::reserveSpaceInContainers(size_t count)
{
    resourceInfos.reserve(count);
    resourceNames.reserve(count);
    resourceAllocations.reserve(count);
    imageViews.reserve(count * 2u); // intended to be used for duplicate handles anyways
    allocInfos.reserve(count);
    resources.reserve(count);
}

bool ResourceContextImpl::infoStorage::mayNeedRehash(const size_t headroom) const noexcept
{
	bool result = false;
	size_t currLoad{ 0u };
	currLoad = static_cast<size_t>(std::floorf(resourceMemoryType.max_load_factor())) * resourceMemoryType.bucket_count();
	result |= (currLoad + headroom) > resourceMemoryType.max_size();
	currLoad = static_cast<size_t>(std::floorf(resourceFlags.max_load_factor())) * resourceFlags.bucket_count();
	result |= (currLoad + headroom) > resourceFlags.max_size();
	currLoad = static_cast<size_t>(std::floorf(bufferInfos.max_load_factor())) * bufferInfos.bucket_count();
	result |= (currLoad + headroom) > bufferInfos.max_size();
	currLoad = static_cast<size_t>(std::floorf(bufferViewInfos.max_load_factor())) * bufferViewInfos.bucket_count();
	result |= (currLoad + headroom) > bufferViewInfos.max_size();
	currLoad = static_cast<size_t>(std::floorf(imageInfos.max_load_factor())) * imageInfos.bucket_count();
	result |= (currLoad + headroom) > imageInfos.max_size();
	currLoad = static_cast<size_t>(std::floorf(imageViewInfos.max_load_factor())) * imageViewInfos.bucket_count();
	result |= (currLoad + headroom) > imageViewInfos.max_size();
	currLoad = static_cast<size_t>(std::floorf(samplerInfos.max_load_factor())) * samplerInfos.bucket_count();
	result |= (currLoad + headroom) > samplerInfos.max_size();
	return result;
}

void ResourceContextImpl::infoStorage::reserve(size_t count)
{
    resourceMemoryType.reserve(count);
	resourceFlags.reserve(count);
    bufferInfos.reserve(count);
    bufferViewInfos.reserve(count);
    imageInfos.reserve(count);
    imageViewInfos.reserve(count);
    samplerInfos.reserve(count);
}

void ResourceContextImpl::infoStorage::clear()
{
	resourceMemoryType.clear();
	resourceFlags.clear();
	bufferInfos.clear();
	bufferViewInfos.clear();
	imageInfos.clear();
	imageViewInfos.clear();
	samplerInfos.clear();
}
