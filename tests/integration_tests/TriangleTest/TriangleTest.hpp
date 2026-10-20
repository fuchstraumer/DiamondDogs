#pragma once
#ifndef VULKAN_TRIANGLE_RENDERER_CONTEXT_TEST_HPP
#define VULKAN_TRIANGLE_RENDERER_CONTEXT_TEST_HPP
#include "VulkanScene.hpp"
#include "CommonCreationFunctions.hpp"
#include "RhiHandle.hpp"
#include "Math.hpp"
#include <vector>

namespace rhi
{
    class CommandPool;
    class ShaderProgram;
}

class VulkanTriangle : public VulkanScene
{
public:

    VulkanTriangle(rhi::RhiSystem* rhi_system, PlatformWindowSystem* platform_system);
    ~VulkanTriangle();

    void Initialize(void* user_data) final;
    void Destroy() final;

protected:

    void update() final;
    void recordCommands() final;
    void draw() final;
    void endFrame() final;
    void onSwapchainRecreated() final;

    void prepareVertices();
    void setupUniformBuffer();
    void setupCommandPool();
    void setupDescriptorPool();
    void setupLayouts();
    void setupDescriptorSet();
    void setupShaderModules();
    void setupDepthStencil();
    void destroyDepthStencil();

    void setInputStates(const rhi::CommandBufferHandle& cmd_buffer);
    void setRasterizationAndSampleStates(const rhi::CommandBufferHandle& cmd_buffer);
    void setColorBlendStates(const rhi::CommandBufferHandle& cmd_buffer);
    void setDepthStencilStates(const rhi::CommandBufferHandle& cmd_buffer);

    void bindPipeline(const rhi::CommandBufferHandle& cmd_buffer);
    void bindGeometryBuffers(const rhi::CommandBufferHandle& cmd_buffer);

    struct Vertex
    {
        math::Float3 position;
        math::Float3 color;
    };

    struct
    {
        VkDeviceMemory memory;
        VkBuffer buffer;
    } Vertices;

    struct
    {
        VkDeviceMemory memory;
        VkBuffer buffer;
        uint32_t count;
    } Indices;

    struct
    {
        VkDeviceMemory memory;
        VkBuffer buffer;
        VkDescriptorBufferInfo descriptor;
    } uniformBufferVS;

    struct
    {
        math::Float4x4 model;
        math::Float4x4 view;
        math::Float4x4 projection;
    } uboDataVS;

    std::vector<DepthStencil> depthStencils;
    VkPipelineLayout pipelineLayout{ VK_NULL_HANDLE };
    // never created: this test draws with shader objects. Kept null so that teardown's vkDestroyPipeline is a no-op
    VkPipeline pipeline{ VK_NULL_HANDLE };
    VkDescriptorSetLayout setLayout{ VK_NULL_HANDLE };
    VkDescriptorSet descriptorSet{ VK_NULL_HANDLE };
    VkDescriptorPool descriptorPool{ VK_NULL_HANDLE };
    VkSubmitInfo submitInfo;
    std::vector<std::unique_ptr<rhi::CommandPool>> commandPools;
    std::unique_ptr<rhi::ShaderProgram> shaderProgram;

    bool setup = false;
};


#endif //!VULKAN_TRIANGLE_RENDERER_CONTEXT_TEST_HPP
