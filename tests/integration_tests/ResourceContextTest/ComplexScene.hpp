#pragma once
#ifndef VULKAN_COMPLEX_SCENE_HPP
#define VULKAN_COMPLEX_SCENE_HPP
#include "VulkanScene.hpp"
#include "ForwardDecl.hpp"
#include "CommonCreationFunctions.hpp"
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <atomic>

struct LoadedObjModel {
    LoadedObjModel(const char* fname);
    struct vertex_t {
        bool operator==(const vertex_t& other) const noexcept {
            return (pos == other.pos) && (uv == other.uv);
        }
        glm::vec3 pos;
        glm::vec2 uv;
    };
    std::vector<vertex_t> vertices;
    std::vector<uint32_t> indices;
};

class ResourceContext;
struct VulkanResource;

class VulkanComplexScene : public VulkanScene {
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

    struct ubo_data_t {
        glm::mat4 model{ glm::mat4(1.0f) };
        glm::mat4 view{ glm::mat4(1.0f) };
        glm::mat4 projection{ glm::mat4(1.0f) };
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
    void createFences();
    void createCommandPool();
    void createDescriptorPool();
    void createDescriptorSetLayouts();
    void createDescriptorSets();
    void createUpdateTemplates();
    void createPipelineLayouts();
    void createShaders();
    void createFramebuffers();
    void createRenderpass();
    void createHousePipeline();
    void createSkyboxPipeline();

    void destroyFences();
    void destroyFramebuffers();

    // has to wait for loading to complete
    void updateHouseDescriptorSet();
    void updateSkyboxDescriptorSet();

    ResourceContext* resourceContext{ nullptr };
    VulkanResource* sampler{ nullptr };
    VulkanResource* houseVBO{ nullptr };
    VulkanResource* houseEBO{ nullptr };
    VulkanResource* houseTexture{ nullptr };
    VulkanResource* skyboxEBO{ nullptr };
    VulkanResource* skyboxVBO{ nullptr };
    VulkanResource* skyboxTexture{ nullptr };
    VulkanResource* houseUBO{ nullptr };
    VulkanResource* skyboxUBO{ nullptr };

    DepthStencil depthStencil;
    std::unique_ptr<vpr::CommandPool> cmdPool{ nullptr };
    std::unique_ptr<vpr::ShaderModule> houseVert{ nullptr };
    std::unique_ptr<vpr::ShaderModule> houseFrag{ nullptr };
    std::unique_ptr<vpr::ShaderModule> skyboxVert{ nullptr };
    std::unique_ptr<vpr::ShaderModule> skyboxFrag{ nullptr };
    std::unique_ptr<vpr::PipelineLayout> pipelineLayout{ nullptr };
    std::unique_ptr<vpr::DescriptorPool> descriptorPool{ nullptr };
    std::unique_ptr<vpr::DescriptorSetLayout> setLayout{ nullptr };
    std::unique_ptr<vpr::DescriptorSet> houseSet{ nullptr };
    std::unique_ptr<vpr::DescriptorSet> skyboxSet{ nullptr };
    std::unique_ptr<vpr::DescriptorSet> baseSet{ nullptr };
    VkDescriptorUpdateTemplate houseTemplate{ VK_NULL_HANDLE };
    VkDescriptorUpdateTemplate skyboxTemplate{ VK_NULL_HANDLE };
    std::unique_ptr<vpr::PipelineCache> houseCache{ nullptr };
    std::unique_ptr<vpr::PipelineCache> skyboxCache{ nullptr };
    std::unique_ptr<vpr::PipelineCache> sharedCache{ nullptr };
    VkPipeline housePipeline{ VK_NULL_HANDLE };
    VkPipeline skyboxPipeline{ VK_NULL_HANDLE };
    VkRenderPass renderPass{ VK_NULL_HANDLE };
    std::vector<VkFence> fences;
    std::vector<VkFramebuffer> framebuffers;

    uint32_t houseIndexCount{ 0u };
    uint32_t skyboxIndexCount{ 0u };
    std::atomic<bool> houseTextureReady{ false };
    std::atomic<bool> houseMeshReady{ false };
    std::atomic<bool> skyboxTextureReady{ false };

};


#endif //!VULKAN_COMPLEX_SCENE_HPP
