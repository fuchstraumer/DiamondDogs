#pragma once
#ifndef VULKAN_TRIANGLE_RENDERER_CONTEXT_TEST_HPP
#define VULKAN_TRIANGLE_RENDERER_CONTEXT_TEST_HPP
#include "VulkanScene.hpp"
#include "CommonCreationFunctions.hpp"
#include "Math.hpp"
#include <vector>

class VulkanTriangle : public VulkanScene
{
protected:
    VulkanTriangle();
    ~VulkanTriangle();
public:

    static VulkanTriangle& GetScene();

    void Construct(RequiredVprObjects objects, void* user_data) final;
    void Destroy() final;

protected:

    void update() final;
    void recordCommands() final;
    void draw() final;
    void endFrame() final;

    void prepareVertices();
    void setupUniformBuffer();
    void setupCommandPool();
    void setupCommandBuffers();
    void setupDescriptorPool();
    void setupLayouts();
    void setupDescriptorSet();
    void setupShaderModules();
    void setupDepthStencil();
    void setupPipeline();

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
    VkPipelineLayout pipelineLayout;
    VkPipeline pipeline;
    VkDescriptorSetLayout setLayout;
    VkDescriptorSet descriptorSet;
    VkDescriptorPool descriptorPool;
    VkSubmitInfo submitInfo;
    VkCommandPool commandPool;
    VkShaderModule vertexShader = VK_NULL_HANDLE;
    VkShaderModule fragmentShader = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> drawCmdBuffers;

    bool setup = false;
};


#endif //!VULKAN_TRIANGLE_RENDERER_CONTEXT_TEST_HPP
