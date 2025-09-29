#include "CreateInfoWrappers.hpp"

// every struct in the chain can be handled this way
struct VkBaseInfoStruct
{
    VkStructureType sType = VK_STRUCTURE_TYPE_MAX_ENUM;
    const void* pNext = nullptr;
};

void HandleBufferPNext(ResourceContextBufferCreateInfo& buffer_info, const VkBufferCreateInfo& vk_buffer_info)
{
    // Traverse the pNext chain and copy relevant structures
    const VkBaseInfoStruct* current = reinterpret_cast<const VkBaseInfoStruct*>(vk_buffer_info.pNext);
    while (current)
    {
        switch (current->sType)
        {
            case VK_STRUCTURE_TYPE_BUFFER_USAGE_FLAGS_2_CREATE_INFO:
                buffer_info.UsageFlags2Info = *reinterpret_cast<const VkBufferUsageFlags2CreateInfo*>(current);
                break;
            case VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_CREATE_INFO_EXT:
                buffer_info.DeviceAddressInfo = *reinterpret_cast<const VkBufferDeviceAddressCreateInfoEXT*>(current);
                break;
            case VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO:
                buffer_info.ExternalMemoryInfo = *reinterpret_cast<const VkExternalMemoryBufferCreateInfo*>(current);
                break;
            case VK_STRUCTURE_TYPE_DEDICATED_ALLOCATION_BUFFER_CREATE_INFO_NV:
                buffer_info.DedicatedAllocInfoNV = *reinterpret_cast<const VkDedicatedAllocationBufferCreateInfoNV*>(current);
                break;
            default:
                // Unknown or unhandled pNext structure; skip
                break;
        }
        current = reinterpret_cast<const VkBaseInfoStruct*>(current->pNext);
    }
}

void HandleImagePNext(ResourceContextImageCreateInfo& image_info, const VkImageCreateInfo& vk_image_info)
{
    // Traverse the pNext chain and copy relevant structures
    const VkBaseInfoStruct* current = reinterpret_cast<const VkBaseInfoStruct*>(vk_image_info.pNext);
    while (current)
    {
        switch (current->sType)
        {
            case VK_STRUCTURE_TYPE_DEDICATED_ALLOCATION_IMAGE_CREATE_INFO_NV:
                image_info.DedicatedAllocInfoNV = *reinterpret_cast<const VkDedicatedAllocationImageCreateInfoNV*>(current);
                break;
            case VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO:
                image_info.ExternalMemoryInfo = *reinterpret_cast<const VkExternalMemoryImageCreateInfo*>(current);
                break;
            case VK_STRUCTURE_TYPE_IMAGE_STENCIL_USAGE_CREATE_INFO:
                image_info.StencilUsageInfo = *reinterpret_cast<const VkImageStencilUsageCreateInfo*>(current);
                break;
            case VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO:
                image_info.FormatListInfo = *reinterpret_cast<const VkImageFormatListCreateInfo*>(current);
                break;
            case VK_STRUCTURE_TYPE_IMAGE_SWAPCHAIN_CREATE_INFO_KHR:
                image_info.SwapchainInfo = *reinterpret_cast<const VkImageSwapchainCreateInfoKHR*>(current);
                break;
            default:
                // Unknown or unhandled pNext structure; skip
                break;
        }
        current = reinterpret_cast<const VkBaseInfoStruct*>(current->pNext);
    }
}

ResourceContextBufferCreateInfo::ResourceContextBufferCreateInfo(const VkBufferCreateInfo& vk_buffer_info) :
    CreateFlags(vk_buffer_info.flags),
    Size(vk_buffer_info.size),
    UsageFlags(vk_buffer_info.usage),
    SharingMode(vk_buffer_info.sharingMode),
    UsageFlags2Info{ std::nullopt },
    DeviceAddressInfo{ std::nullopt },
    ExternalMemoryInfo{ std::nullopt },
    DedicatedAllocInfoNV{ std::nullopt }
{
    if ((vk_buffer_info.sharingMode & VK_SHARING_MODE_CONCURRENT) && vk_buffer_info.pQueueFamilyIndices != nullptr)
    {
        QueueFamilyIndices.assign(vk_buffer_info.pQueueFamilyIndices, 
                                vk_buffer_info.pQueueFamilyIndices + vk_buffer_info.queueFamilyIndexCount);
    }

    HandleBufferPNext(*this, vk_buffer_info);
}

ResourceContextBufferCreateInfo::operator VkBufferCreateInfo() const noexcept
{
    VkBufferCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    create_info.pNext = nullptr;
    create_info.flags = CreateFlags;
    create_info.size = Size;
    create_info.usage = UsageFlags;
    create_info.sharingMode = SharingMode;
    
    if ((SharingMode & VK_SHARING_MODE_CONCURRENT) && !QueueFamilyIndices.empty())
    {
        create_info.queueFamilyIndexCount = static_cast<uint32_t>(QueueFamilyIndices.size());
        create_info.pQueueFamilyIndices = QueueFamilyIndices.data();
    }
    else
    {
        create_info.queueFamilyIndexCount = 0;
        create_info.pQueueFamilyIndices = nullptr;
    }

    // this is actually quite annoying isn't it. thank you vulkan for being so extensible <3 <3 <3 /s
    void* last_pnext = nullptr;
    if (UsageFlags2Info.has_value())
    {
        if (last_pnext == nullptr)
        {
            create_info.pNext = &UsageFlags2Info.value();
        }
        else
        {
            reinterpret_cast<VkBaseInfoStruct*>(last_pnext)->pNext = reinterpret_cast<const void*>(&UsageFlags2Info.value());
        }
        last_pnext = (void*)&UsageFlags2Info.value();
    }

    if (DeviceAddressInfo.has_value())
    {
        if (last_pnext == nullptr)
        {
            create_info.pNext = &DeviceAddressInfo.value();
        }
        else
        {
            reinterpret_cast<VkBaseInfoStruct*>(last_pnext)->pNext = &DeviceAddressInfo.value();
        }
        last_pnext = (void*)&DeviceAddressInfo.value();
    }

    if (ExternalMemoryInfo.has_value())
    {
        if (last_pnext == nullptr)
        {
            create_info.pNext = &ExternalMemoryInfo.value();
        }
        else
        {
            reinterpret_cast<VkBaseInfoStruct*>(last_pnext)->pNext = &ExternalMemoryInfo.value();
        }
        last_pnext = (void*)&ExternalMemoryInfo.value();
    }

    if (DedicatedAllocInfoNV.has_value())
    {
        if (last_pnext == nullptr)
        {
            create_info.pNext = &DedicatedAllocInfoNV.value();
        }
        else
        {
            reinterpret_cast<VkBaseInfoStruct*>(last_pnext)->pNext = &DedicatedAllocInfoNV.value();
        }
        last_pnext = (void*)&DedicatedAllocInfoNV.value();
    }

    create_info.pNext = last_pnext;
    
    return create_info;
}

ResourceContextImageCreateInfo::ResourceContextImageCreateInfo(const VkImageCreateInfo& vk_image_info) :
    CreateFlags(vk_image_info.flags),
    ImageType(vk_image_info.imageType),
    Format(vk_image_info.format),
    Extent(vk_image_info.extent),
    MipLevels(vk_image_info.mipLevels),
    ArrayLayers(vk_image_info.arrayLayers),
    Samples(vk_image_info.samples),
    Tiling(vk_image_info.tiling),
    UsageFlags(vk_image_info.usage),
    SharingMode(vk_image_info.sharingMode),
    InitialLayout(vk_image_info.initialLayout)
{
    if ((vk_image_info.sharingMode & VK_SHARING_MODE_CONCURRENT) && vk_image_info.pQueueFamilyIndices != nullptr)
    {
        QueueFamilyIndices.assign(vk_image_info.pQueueFamilyIndices, 
                                vk_image_info.pQueueFamilyIndices + vk_image_info.queueFamilyIndexCount);
    }

    HandleImagePNext(*this, vk_image_info);
}

ResourceContextImageCreateInfo::operator VkImageCreateInfo() const noexcept
{
    VkImageCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    create_info.flags = CreateFlags;
    create_info.imageType = ImageType;
    create_info.format = Format;
    create_info.extent = Extent;
    create_info.mipLevels = MipLevels;
    create_info.arrayLayers = ArrayLayers;
    create_info.samples = Samples;
    create_info.tiling = Tiling;
    create_info.usage = UsageFlags;
    create_info.sharingMode = SharingMode;
    create_info.initialLayout = InitialLayout;
    
    if ((SharingMode & VK_SHARING_MODE_CONCURRENT) && !QueueFamilyIndices.empty())
    {
        create_info.queueFamilyIndexCount = static_cast<uint32_t>(QueueFamilyIndices.size());
        create_info.pQueueFamilyIndices = QueueFamilyIndices.data();
    }
    else
    {
        create_info.queueFamilyIndexCount = 0;
        create_info.pQueueFamilyIndices = nullptr;
    }

    void* last_pnext = nullptr;
    if (DedicatedAllocInfoNV.has_value())
    {
        if (last_pnext == nullptr)
        {
            create_info.pNext = &DedicatedAllocInfoNV.value();
        }
        else
        {
            reinterpret_cast<VkBaseInfoStruct*>(last_pnext)->pNext = &DedicatedAllocInfoNV.value();
        }
        last_pnext = (void*)&DedicatedAllocInfoNV.value();
    }

    if (ExternalMemoryInfo.has_value())
    {
        if (last_pnext == nullptr)
        {
            create_info.pNext = &ExternalMemoryInfo.value();
        }
        else
        {
            reinterpret_cast<VkBaseInfoStruct*>(last_pnext)->pNext = &ExternalMemoryInfo.value();
        }
        last_pnext = (void*)&ExternalMemoryInfo.value();
    }

    if (StencilUsageInfo.has_value())
    {
        if (last_pnext == nullptr)
        {
            create_info.pNext = &StencilUsageInfo.value();
        }
        else
        {
            reinterpret_cast<VkBaseInfoStruct*>(last_pnext)->pNext = &StencilUsageInfo.value();
        }
        last_pnext = (void*)&StencilUsageInfo.value();
    }

    if (FormatListInfo.has_value())
    {
        if (last_pnext == nullptr)
        {
            create_info.pNext = &FormatListInfo.value();
        }
        else
        {
            reinterpret_cast<VkBaseInfoStruct*>(last_pnext)->pNext = &FormatListInfo.value();
        }
        last_pnext = (void*)&FormatListInfo.value();
    }

    if (SwapchainInfo.has_value())
    {
        if (last_pnext == nullptr)
        {
            create_info.pNext = &SwapchainInfo.value();
        }
        else
        {
            reinterpret_cast<VkBaseInfoStruct*>(last_pnext)->pNext = &SwapchainInfo.value();
        }
        last_pnext = (void*)&SwapchainInfo.value();
    }

    create_info.pNext = last_pnext;
    
    return create_info;
}
