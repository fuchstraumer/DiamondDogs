#include "ResourcePlacement.hpp"
#include "FutureTable.hpp"
#include "ResourceContext.hpp"
#include "ResourceContextImpl.hpp"
#include "ResourceFuture.hpp"
#include "ResourceMessageTypesInternal.hpp"
#include "ResourceTypes.hpp"
#include "VulkanConversions.hpp"
#include <cstdint>
#include <utility>
#include <vulkan/vulkan_core.h>

namespace
{
    /** @brief Returns the answer of a memory requirements query, in the public type. */
    [[nodiscard]] rhi::placement::MemoryRequirements ToMemoryRequirements(const VkMemoryRequirements& requirements) noexcept;
}

namespace rhi::placement
{
    MemoryRequirements GetImageMemoryRequirements(const ResourceContext& context, const ImageCreateInfo& create_info)
    {
        if (create_info.Width == 0u || !create_info.Format.IsValid())
        {
            return MemoryRequirements{};
        }

        const ResourceContextImpl& impl = *context.impl;
        const VkImageCreateInfo vkImageInfo = ToVkImageCreateInfo(create_info, impl.QueueFamilies());
        const VkDeviceImageMemoryRequirements query
        {
            VK_STRUCTURE_TYPE_DEVICE_IMAGE_MEMORY_REQUIREMENTS,
            nullptr,
            &vkImageInfo,
            // only read for a multi-planar image with disjoint planes
            static_cast<VkImageAspectFlagBits>(0)
        };

        VkMemoryRequirements2 requirements{ VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2 };
        vkGetDeviceImageMemoryRequirements(impl.VulkanDevice(), &query, &requirements);
        return ToMemoryRequirements(requirements.memoryRequirements);
    }

    MemoryRequirements GetBufferMemoryRequirements(const ResourceContext& context, const BufferCreateInfo& create_info)
    {
        if (create_info.Size == 0u)
        {
            return MemoryRequirements{};
        }

        const ResourceContextImpl& impl = *context.impl;
        const VkBufferCreateInfo vkBufferInfo = ToVkBufferCreateInfo(create_info, impl.QueueFamilies());
        const VkDeviceBufferMemoryRequirements query
        {
            VK_STRUCTURE_TYPE_DEVICE_BUFFER_MEMORY_REQUIREMENTS,
            nullptr,
            &vkBufferInfo
        };

        VkMemoryRequirements2 requirements{ VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2 };
        vkGetDeviceBufferMemoryRequirements(impl.VulkanDevice(), &query, &requirements);
        return ToMemoryRequirements(requirements.memoryRequirements);
    }

    ResourceFuture<GraphicsResource> CreateMemoryHeap(ResourceContext& context, const MemoryHeapCreateInfo& create_info)
    {
        ResourceContextImpl& impl = *context.impl;
        ResourceFuture<GraphicsResource> future = impl.Futures().AllocateResource();
        const int32_t slot = FutureTable::SlotOf(future);
        if (FutureTable::IsFullTableSlot(slot))
        {
            return future;
        }

        CreateMemoryHeapMessage message;
        message.Size = create_info.Size;
        message.MemoryTypeBits = create_info.MemoryTypeBits;
        message.Name = DebugName(create_info.Name);
        message.Future = slot;

        impl.PushMessage(std::move(message));
        return future;
    }

    ResourceFuture<GraphicsResource> CreatePlacedImage(ResourceContext& context, const ImageCreateInfo& create_info, Placement placement)
    {
        ResourceContextImpl& impl = *context.impl;
        ResourceFuture<GraphicsResource> future = impl.Futures().AllocateResource();
        const int32_t slot = FutureTable::SlotOf(future);
        if (FutureTable::IsFullTableSlot(slot))
        {
            return future;
        }

        CreatePlacedImageMessage message;
        message.Create = MakeCreateImageMessage(create_info, slot);
        message.Heap = placement.Heap;
        message.Offset = placement.Offset;

        impl.PushMessage(std::move(message));
        return future;
    }

    ResourceFuture<GraphicsResource> CreatePlacedBuffer(ResourceContext& context, const BufferCreateInfo& create_info, Placement placement)
    {
        ResourceContextImpl& impl = *context.impl;
        ResourceFuture<GraphicsResource> future = impl.Futures().AllocateResource();
        const int32_t slot = FutureTable::SlotOf(future);
        if (FutureTable::IsFullTableSlot(slot))
        {
            return future;
        }

        CreatePlacedBufferMessage message;
        message.Create = MakeCreateBufferMessage(create_info, slot);
        message.Heap = placement.Heap;
        message.Offset = placement.Offset;

        impl.PushMessage(std::move(message));
        return future;
    }
}

namespace
{
    rhi::placement::MemoryRequirements ToMemoryRequirements(const VkMemoryRequirements& requirements) noexcept
    {
        return rhi::placement::MemoryRequirements
        {
            static_cast<uint64_t>(requirements.size),
            static_cast<uint64_t>(requirements.alignment),
            requirements.memoryTypeBits
        };
    }
}
