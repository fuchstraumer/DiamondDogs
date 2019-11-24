#pragma once
#ifndef CONTENT_COMPILER_TEST_SCENE_HPP
#define CONTENT_COMPILER_TEST_SCENE_HPP
#include "VulkanScene.hpp"
#include "ForwardDecl.hpp"
#include "CommonCreationFunctions.hpp"

class ContentCompilerScene : public VulkanScene
{
    ContentCompilerScene();
    ~ContentCompilerScene();
public:

    static ContentCompilerScene& Get();

    void Construct(RequiredVprObjects objects, void* user_data) final;
    void Destroy() final;

    static void* LoadObjFile(const char* fname, void* user_data);
    static void DestroyObjFile(void* obj_file, void* user_data);

    void CreateMesh(void* obj_data);

protected:

    void update() final;
    void recordCommands() final;
    void renderObject(VkCommandBuffer cmd);
    void draw() final;
    void endFrame() final;

    VulkanResource* meshVBO{ nullptr };
    VulkanResource* meshEBO{ nullptr };
    VulkanResource* sampler{ nullptr };
    VulkanResource* meshUBO{ nullptr };

    DepthStencil depthStencil;
    std::unique_ptr<vpr::CommandPool> cmdPool;
    std::unique_ptr<vpr::ShaderModule> vertexShader, fragmentShader;
    std::unique_ptr<vpr::PipelineLayout> pipelineLayout;
    std::unique_ptr<vpr::DescriptorPool> descriptorPool;
    std::unique_ptr<vpr::DescriptorSetLayout> descriptorSetLayout;
    std::unique_ptr<vpr::DescriptorSet> descriptorSet;
    std::unique_ptr<vpr::PipelineCache> pipelineCache;
    VkPipeline modelPipeline{ VK_NULL_HANDLE };
    VkRenderPass renderPass;
    std::vector<VkFence> fences;
    std::vector<VkFramebuffer> framebuffers;

    uint32_t modelIndexCount;

};

#endif //!CONTENT_COMPILER_TEST_SCENE_HPP
