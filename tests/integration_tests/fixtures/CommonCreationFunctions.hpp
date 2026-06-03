#pragma once
#ifndef DIAMOND_DOGS_TESTS_COMMON_CREATION_FUNCTIONS_HPP
#define DIAMOND_DOGS_TESTS_COMMON_CREATION_FUNCTIONS_HPP
#include <vulkan/vulkan.h>
#include <array>

namespace rhi
{
    class Instance;
    class Device;
}

class Swapchain;

struct DepthStencil
{
    DepthStencil();
    DepthStencil(const rhi::Device* device, const Swapchain* swap);
    ~DepthStencil();

    DepthStencil(const DepthStencil&) = delete;
    DepthStencil& operator=(const DepthStencil&) = delete;

    DepthStencil(DepthStencil&& other) noexcept;
    DepthStencil& operator=(DepthStencil&& other) noexcept;

    VkImage Image{ VK_NULL_HANDLE };
    VkDeviceMemory Memory{ VK_NULL_HANDLE };
    VkImageView View{ VK_NULL_HANDLE };
    VkFormat Format;
    VkDevice Parent{ VK_NULL_HANDLE };
};

struct BasicPipelineCreateInfo
{
    const rhi::Device* device{ nullptr };
    VkPipelineCreateFlags pipelineFlags{ 0 };
    uint32_t numStages{ 0u };
    const VkPipelineShaderStageCreateInfo* stages{ nullptr };
    const VkPipelineVertexInputStateCreateInfo* vertexState{ nullptr };
    VkPipelineLayout pipelineLayout{ VK_NULL_HANDLE };
    const VkPipelineRenderingCreateInfo* renderingCreateInfo{ nullptr };
    VkCompareOp depthCompareOp;
    VkPipelineCache pipelineCache{ VK_NULL_HANDLE };
    VkPipeline derivedPipeline{ VK_NULL_HANDLE };
    VkCullModeFlags cullMode{ VK_CULL_MODE_BACK_BIT };
    VkPrimitiveTopology topology{ VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST };
};

DepthStencil CreateDepthStencil(const rhi::Device* device, const Swapchain* swapchain);
VkPipeline CreateBasicPipeline(const BasicPipelineCreateInfo& createInfo);

#endif //!DIAMOND_DOGS_TESTS_COMMON_CREATION_FUNCTIONS_HPP