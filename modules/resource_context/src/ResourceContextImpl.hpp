#pragma once
#ifndef RESOURCE_CONTEXT_IMPL_HPP
#define RESOURCE_CONTEXT_IMPL_HPP
#include "ResourceTypes.hpp"
#include "ResourceMessageReply.hpp"
#include "ResourceMessageTypesInternal.hpp"
#include "TransferSystem.hpp"
#include "ResourceLoader.hpp"
#include "LogicalDevice.hpp"
#include "PhysicalDevice.hpp"
#include "vkAssert.hpp"
#include "UploadBuffer.hpp"
#include "VkDebugUtils.hpp"
#include "containers/mwsrQueue.hpp"

#include <atomic>
#include <memory>
#include <thread>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <string>

#include <vulkan/vulkan_core.h>
#include <vk_mem_alloc.h>
#include <entt/entity/registry.hpp>
#include "threading/ExponentialBackoffSleeper.hpp"

struct ResourceContextCreateInfo;

class ResourceContextImpl
{
public:

    void construct(const ResourceContextCreateInfo& createInfo);
    void destroy();
    // locks current thread and forces it to wait for all pending operations to complete, unlike how it used to be (required per-frame ticks)
    void update();

    void pushMessage(ResourceMessagePayloadType message);
    void setExitWorker();
    void startWorker();

private:

    struct ResourceFlags
    {
        resource_type type;
        resource_creation_flags flags;
        resource_usage resourceUsage;
    };

    void processMessages();

    // I don't want to blow up the header implementing the above functions, so they're redeclared here explicitly to be implemented in the .cpp. Sorry :(
    void processCreateBufferMessage(CreateBufferMessage&& message);
    void processCreateImageMessage(CreateImageMessage&& message);
    void processCreateCombinedImageSamplerMessage(CreateCombinedImageSamplerMessage&& message);
    void processCreateSamplerMessage(CreateSamplerMessage&& message);
    void processSetBufferDataMessage(SetBufferDataMessage&& message);
    void processSetImageDataMessage(SetImageDataMessage&& message);
    void processFillResourceMessage(FillResourceMessage&& message);
    void processMapResourceMessage(MapResourceMessage&& message);
    void processUnmapResourceMessage(UnmapResourceMessage&& message);
    void processCopyResourceMessage(CopyResourceMessage&& message);
    void processCopyResourceContentsMessage(CopyResourceContentsMessage&& message);
    void processDestroyResourceMessage(DestroyResourceMessage&& message);

    VkBuffer createBuffer(
        entt::entity new_entity,
        VkBufferCreateInfo&& buffer_info,
        const resource_creation_flags _flags,
        const resource_usage _resource_usage,
        void* user_data_ptr,
        bool has_initial_data);

    VkBufferView createBufferView(
        entt::entity new_entity,
        VkBufferViewCreateInfo&& view_info,
        const resource_creation_flags _flags,
        void* user_data_ptr);

    // in case of CPU-only buffers (e.g. UBOs and such) we just handle it in the resource context instead of the transfer system
    void setBufferDataHostOnly(entt::entity new_entity, InternalResourceDataContainer& dataContainer);

    VkImage createImage(
        entt::entity new_entity,
        VkImageCreateInfo&& image_info,
        const resource_creation_flags _flags,
        const resource_usage _resource_usage,
        void* user_data_ptr,
        bool has_initial_data);
 
    VkImageView createImageView(
        entt::entity new_entity,
        const VkImageViewCreateInfo& view_info,
        const resource_creation_flags _flags);

    VkSampler createSampler(
        entt::entity new_entity,
        const VkSamplerCreateInfo& sampler_info,
        const resource_creation_flags _flags,
        void* user_data_ptr);

    MessageReply::Status destroyBuffer(entt::entity entity, GraphicsResource resource);
    MessageReply::Status destroyImage(entt::entity entity, GraphicsResource resource);
    MessageReply::Status destroySampler(entt::entity entity, GraphicsResource resource);
    MessageReply::Status destroyBufferView(entt::entity entity, GraphicsResource resource);
    MessageReply::Status destroyImageView(entt::entity entity, GraphicsResource resource);
    MessageReply::Status destroyCombinedImageSampler(entt::entity entity, GraphicsResource resource);

    void writeStatsJsonFile(const char* output_file);

    mwsrQueue<ResourceMessagePayloadType> messageQueue;
    std::thread workerThread;
    std::atomic<bool> shouldExitWorker{ false };

    vpr::VkDebugUtilsFunctions vkDebugFns;
    VmaAllocator allocatorHandle{ VK_NULL_HANDLE };

    // Primarily used to contain our info structures and allocation structures
    entt::registry resourceRegistry;
    ResourceTransferSystem transferSystem;

    const vpr::Device* device = nullptr;
    bool validationEnabled{ false };
};


#endif //!RESOURCE_CONTEXT_IMPL_HPP
