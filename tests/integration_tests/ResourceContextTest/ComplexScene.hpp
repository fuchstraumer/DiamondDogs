#pragma once
#ifndef VULKAN_COMPLEX_SCENE_HPP
#define VULKAN_COMPLEX_SCENE_HPP
#include "VulkanScene.hpp"
#include "ForwardDecl.hpp"
#include "CommonCreationFunctions.hpp"
#include "ResourceTypes.hpp"
#include "ResourceMessageReply.hpp"
#include <vector>
#include <atomic>
#include "Math.hpp"

using namespace math;

struct LoadedObjModel
{
    LoadedObjModel(const char* fname);
    struct vertex_t
    {
        bool operator==(const vertex_t& other) const noexcept
        {
            return (pos == other.pos) && (uv == other.uv);
        }
        Float3 pos;
        Float2 uv;
    };
    std::vector<vertex_t> vertices;
    std::vector<uint32_t> indices;
};

class ResourceContext;

class VulkanComplexScene : public VulkanScene
{
    VulkanComplexScene();
    ~VulkanComplexScene();
public:

    static VulkanComplexScene& GetScene();

    void Construct(RequiredVprObjects objects, void* user_data) final;
    void Destroy() final;

    static void* LoadObjFile(const char* fname, void* user_data = nullptr);
    static void DestroyObjFileData(void* obj_file, void* user_data);
    static void* LoadPngImage(const char* fname, void* user_data = nullptr);
    static void DestroyPngFileData(void* jpeg_file, void* user_data);
    static void* LoadCompressedTexture(const char* fname, void* user_data = nullptr);
    static void DestroyCompressedTextureData(void* compressed_texture, void* user_data);

    void CreateHouseMesh(void* obj_data);
    void CreateHouseTexture(void* texture_data);
    void CreateSkyboxTexture(void* texture_data);

    bool AllAssetsLoaded();
    void WaitForAllLoaded();

    struct ubo_data_t
    {
        Float4x4 model{};
        Float4x4 view{};
        Float4x4 projection{};
    };
    ubo_data_t houseUboData;
    ubo_data_t skyboxUboData;

protected:

    void update() final;
    void recordCommands() final;
    void renderHouse(VkCommandBuffer cmd);
    void renderSkybox(VkCommandBuffer cmd);
    void draw() final;
    void endFrame() final;

    void createSampler();
    void createUBOs();
    void createSkyboxMesh();
    void createCommandPool();
    void createDescriptorPool();
    void createDescriptorSetLayouts();
    void createDescriptorSets();
    void createUpdateTemplates();
    void createPipelineLayouts();
    void createShaders();
    void createHousePipeline();
    void createSkyboxPipeline();


    // has to wait for loading to complete
    void updateHouseDescriptorSet();
    void updateSkyboxDescriptorSet();

    ResourceContext* resourceContext;
    GraphicsResource sampler;
    GraphicsResource houseVBO;
    GraphicsResource houseEBO;
    GraphicsResource houseTexture;
    GraphicsResource skyboxEBO;
    GraphicsResource skyboxVBO;
    GraphicsResource skyboxTexture;
    GraphicsResource houseUBO;
    void* houseUboMappedPtr;
    GraphicsResource skyboxUBO;
    void* skyboxUboMappedPtr;

    std::vector<DepthStencil> depthStencils;
    std::unique_ptr<vpr::CommandPool> cmdPool;
    std::unique_ptr<vpr::ShaderModule> houseVert;
    std::unique_ptr<vpr::ShaderModule> houseFrag;
    std::unique_ptr<vpr::ShaderModule> skyboxVert;
    std::unique_ptr<vpr::ShaderModule> skyboxFrag;
    std::unique_ptr<vpr::PipelineLayout> pipelineLayout;
    std::unique_ptr<vpr::DescriptorPool> descriptorPool;
    std::unique_ptr<vpr::DescriptorSetLayout> setLayout;
    std::unique_ptr<vpr::DescriptorSet> houseSet;
    std::unique_ptr<vpr::DescriptorSet> skyboxSet;
    std::unique_ptr<vpr::DescriptorSet> baseSet;
    VkDescriptorUpdateTemplate houseTemplate{ VK_NULL_HANDLE };
    VkDescriptorUpdateTemplate skyboxTemplate{ VK_NULL_HANDLE };
    std::unique_ptr<vpr::PipelineCache> houseCache;
    std::unique_ptr<vpr::PipelineCache> skyboxCache;
    std::unique_ptr<vpr::PipelineCache> sharedCache;
    VkPipeline housePipeline{ VK_NULL_HANDLE };
    VkPipeline skyboxPipeline{ VK_NULL_HANDLE };
    VkRenderPass renderPass{ VK_NULL_HANDLE };

    uint32_t houseIndexCount{ 0u };
    uint32_t skyboxIndexCount{ 0u };
    std::shared_ptr<GraphicsResourceReply> samplerReply;
    std::shared_ptr<GraphicsResourceReply> skyboxTextureReply;
    std::shared_ptr<GraphicsResourceReply> houseTextureReply;
    std::shared_ptr<GraphicsResourceReply> houseVboReply;
    std::shared_ptr<GraphicsResourceReply> houseEboReply;
    std::shared_ptr<GraphicsResourceReply> skyboxVboReply;
    std::shared_ptr<GraphicsResourceReply> skyboxEboReply;

};

#endif //!VULKAN_COMPLEX_SCENE_HPP
