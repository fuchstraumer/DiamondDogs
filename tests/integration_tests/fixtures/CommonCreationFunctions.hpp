#pragma once
#ifndef DIAMOND_DOGS_TESTS_COMMON_CREATION_FUNCTIONS_HPP
#define DIAMOND_DOGS_TESTS_COMMON_CREATION_FUNCTIONS_HPP
#include <vulkan/vulkan.h>
#include <array>

namespace vpr {
    class Instance;
    class PhysicalDevice;
    class Device;
    class Swapchain;
}

struct DepthStencil {
    DepthStencil() = default;
    DepthStencil(const vpr::Device* device, const vpr::PhysicalDevice* p_device, const vpr::Swapchain* swap);
    ~DepthStencil();
    VkImage Image{ VK_NULL_HANDLE };
    VkDeviceMemory Memory{ VK_NULL_HANDLE };
    VkImageView View{ VK_NULL_HANDLE };
    VkFormat Format;
    VkDevice Parent{ VK_NULL_HANDLE };
};

struct BasicPipelineCreateInfo
{
    const vpr::Device* device{ nullptr };
    VkPipelineCreateFlags pipelineFlags{ 0 };
    uint32_t numStages{ 0u };
    const VkPipelineShaderStageCreateInfo* stages{ nullptr };
    const VkPipelineVertexInputStateCreateInfo* vertexState{ nullptr };
    VkPipelineLayout pipelineLayout{ VK_NULL_HANDLE };
    VkRenderPass renderPass{ VK_NULL_HANDLE };
    VkCompareOp depthCompareOp;
    VkPipelineCache pipelineCache{ VK_NULL_HANDLE };
    VkPipeline derivedPipeline{ VK_NULL_HANDLE };
    VkCullModeFlags cullMode{ VK_CULL_MODE_BACK_BIT };
    VkPrimitiveTopology topology{ VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST };
};

uint32_t GetMemoryTypeIndex(uint32_t type_bits, VkMemoryPropertyFlags properties, VkPhysicalDeviceMemoryProperties memory_properties);
DepthStencil CreateDepthStencil(const vpr::Device* device, const vpr::PhysicalDevice* physical_device, const vpr::Swapchain* swapchain);
VkRenderPass CreateBasicRenderpass(const vpr::Device* device, const vpr::Swapchain* swapchain, VkFormat depth_format);
VkPipeline CreateBasicPipeline(const BasicPipelineCreateInfo& createInfo);

#endif //!DIAMOND_DOGS_TESTS_COMMON_CREATION_FUNCTIONS_HPP