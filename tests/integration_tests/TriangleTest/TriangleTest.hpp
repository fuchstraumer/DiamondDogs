#pragma once
#ifndef VULKAN_TRIANGLE_RENDERER_CONTEXT_TEST_HPP
#define VULKAN_TRIANGLE_RENDERER_CONTEXT_TEST_HPP
#include "VulkanScene.hpp"
#include "CommonCreationFunctions.hpp"
#include <vector>
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

class VulkanTriangle : public VulkanScene {
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
    void setupRenderpass();
    void setupPipeline();
    void setupFramebuffers();
    void setupSyncPrimitives();

    struct Vertex {
        float position[3];
        float color[3];
    };

    struct {
        VkDeviceMemory memory;
        VkBuffer buffer;
    } Vertices;

    struct {
        VkDeviceMemory memory;
        VkBuffer buffer;
        uint32_t count;
    } Indices;

    struct {
        VkDeviceMemory memory;
        VkBuffer buffer;
        VkDescriptorBufferInfo descriptor;
    } uniformBufferVS;

    struct {
        glm::mat4 model;
        glm::mat4 view;
        glm::mat4 projection;
    } uboDataVS;

    DepthStencil depthStencil;
    VkPipelineLayout pipelineLayout;
    VkPipeline pipeline;
    VkDescriptorSetLayout setLayout;
    VkDescriptorSet descriptorSet;
    VkDescriptorPool descriptorPool;
    VkRenderPass renderpass;
    std::vector<VkFence> fences;
    std::vector<VkFramebuffer> framebuffers;
    VkSubmitInfo submitInfo;
    VkCommandPool commandPool;
    VkShaderModule vertexShader = VK_NULL_HANDLE;
    VkShaderModule fragmentShader = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> drawCmdBuffers;

    bool setup = false;
};


#endif //!VULKAN_TRIANGLE_RENDERER_CONTEXT_TEST_HPP
