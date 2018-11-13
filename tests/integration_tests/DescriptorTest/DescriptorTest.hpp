#pragma once
#ifndef DESCRIPTOR_TEST_HPP
#define DESCRIPTOR_TEST_HPP
#include "VulkanScene.hpp"
#include "ForwardDecl.hpp"
#include "vulkan/vulkan.h"
#include "Descriptor.hpp"
#include "CommonCreationFunctions.hpp"
#include <vector>
#include <atomic>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

struct VulkanResource;
class ResourceContext;

class DescriptorTest : public VulkanScene {
    DescriptorTest();
    ~DescriptorTest();
public:

    static DescriptorTest& Get();

    void Construct(RequiredVprObjects objects, void* user_data) final;
    void Destroy() final;

    static void* LoadCompressedTexture(const char* fname, void* user_data = nullptr);
    static void DestroyCompressedTexture(void* texture);

    void CreateSkyboxTexture(void* texture_data);

    void ChangeTexture();

    struct ubo_data_t {
        glm::mat4 model;
        glm::mat4 view;
        glm::mat4 projection;
    };
    ubo_data_t houseUboData;
    ubo_data_t skyboxUboData;

private:

    void update() final;
    void recordCommands() final;
    void renderHouse(VkCommandBuffer cmd);
    void renderSkybox(VkCommandBuffer cmd);
    void draw() final;
    void endFrame() final;

    void createSampler();
    void createDepthStencil();
    void createUBO();
    void createSkyboxMesh();
    void createFences();
    void createCommandPool();
    void createDescriptorPool();
    void createDescriptor();
    void createPipelineLayout();
    void createShaders();
    void createRenderpass();
    void createFramebuffers();
    void createPipeline();

    void destroyFences();
    void destroyFramebuffers();

    ResourceContext* resourceContext;

    VulkanResource* sampler{ nullptr };
    VulkanResource* skyboxTexture0{ nullptr };
    VulkanResource* skyboxTexture1{ nullptr };
    VulkanResource* skyboxEBO{ nullptr };
    VulkanResource* skyboxVBO{ nullptr };
    VulkanResource* skyboxUBO{ nullptr };

    DepthStencil depthStencil;
    std::unique_ptr<Descriptor> descriptor{ nullptr };
    std::unique_ptr<vpr::CommandPool> cmdPool{ nullptr };
    std::unique_ptr<vpr::DescriptorPool> descriptorPool{ nullptr };
    std::unique_ptr<vpr::PipelineLayout> pipelineLayout{ nullptr };
    std::unique_ptr<vpr::ShaderModule> vert{ nullptr };
    std::unique_ptr<vpr::ShaderModule> frag{ nullptr };
    std::unique_ptr<vpr::PipelineCache> pipelineCache{ nullptr };
    VkPipeline pipeline{ VK_NULL_HANDLE };
    VkRenderPass renderPass{ VK_NULL_HANDLE };
    std::vector<VkFence> fences;
    std::vector<VkFramebuffer> framebuffers;
    uint32_t skyboxIndexCount{ 0 };

    std::atomic<bool> skyboxTextureReady{ false };

};

#endif // !DESCRIPTOR_TEST_HPP
