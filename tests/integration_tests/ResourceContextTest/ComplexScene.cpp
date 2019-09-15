#include "ComplexScene.hpp"
#include "PipelineCache.hpp"
#include "DescriptorPool.hpp"
#include "DescriptorSet.hpp"
#include "DescriptorSetLayout.hpp"
#include "PipelineLayout.hpp"
#include "ShaderModule.hpp"
#include "LogicalDevice.hpp"
#include "Swapchain.hpp"
#include "PhysicalDevice.hpp"
#include "CommandPool.hpp"
#include "Semaphore.hpp"
#include "ResourceTypes.hpp"
#include "ResourceContext.hpp"
#include "TransferSystem.hpp"
#include "vkAssert.hpp"
#include "GraphicsPipeline.hpp"
#include "HouseShaders.hpp"
#include "SkyboxShaders.hpp"
#pragma warning(push, 1)
#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"
#include "gli/gli.hpp"
#include "../../../third_party/easyloggingpp/src/easylogging++.h"
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/hash.hpp>
#define TINYOBJ_LOADER_OPT_IMPLEMENTATION 
#include <tinyobjloader/experimental/tinyobj_loader_opt.h>
#include <mango/core/memory.hpp>
#include <mango/filesystem/filesystem.hpp>
#pragma warning(pop)
#include <iostream>
#include <array>

const static std::array<glm::vec3, 8> skybox_positions {
    glm::vec3(-1.0f,-1.0f, 1.0f ),
    glm::vec3( 1.0f,-1.0f, 1.0f ),
    glm::vec3( 1.0f, 1.0f, 1.0f ),
    glm::vec3(-1.0f, 1.0f, 1.0f ),
    glm::vec3( 1.0f,-1.0f,-1.0f ),
    glm::vec3(-1.0f,-1.0f,-1.0f ),
    glm::vec3(-1.0f, 1.0f,-1.0f ),
    glm::vec3( 1.0f, 1.0f,-1.0f )
};

struct skybox_mesh_data_t {
    skybox_mesh_data_t() {
        auto add_vertex = [&](const glm::vec3& v)->uint32_t {
            vertices.emplace_back(v);
            return static_cast<uint32_t>(vertices.size() - 1);
        };

        auto build_face = [&](const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2, const glm::vec3& v3) {
            uint32_t i0 = add_vertex(v0);
            uint32_t i1 = add_vertex(v1);
            uint32_t i2 = add_vertex(v2);
            uint32_t i3 = add_vertex(v3);

            indices.insert(indices.end(), { i0, i1, i2 });
            indices.insert(indices.end(), { i0, i2, i3 });        
        };

        build_face(skybox_positions[0], skybox_positions[1], skybox_positions[2], skybox_positions[3]);
        build_face(skybox_positions[1], skybox_positions[4], skybox_positions[7], skybox_positions[2]);
        build_face(skybox_positions[3], skybox_positions[2], skybox_positions[7], skybox_positions[6]);
        build_face(skybox_positions[5], skybox_positions[0], skybox_positions[3], skybox_positions[6]);
        build_face(skybox_positions[5], skybox_positions[4], skybox_positions[1], skybox_positions[0]);
        build_face(skybox_positions[4], skybox_positions[5], skybox_positions[6], skybox_positions[7]);
    }

    std::vector<glm::vec3> vertices;
    std::vector<uint32_t> indices;
};

struct stb_image_data_t {
    stb_image_data_t(const char* fname) {
        pixels = stbi_load(fname, &width, &height, &channels, 4);
        if (!pixels) {
            throw std::runtime_error("Invalid file path for stb_load");
        }
    }
    ~stb_image_data_t() {
        if (pixels) {
            stbi_image_free(pixels);
        }
        pixels = nullptr;
    }
    stbi_uc* pixels = nullptr;
    int width = -1;
    int height = -1;
    int channels = -1;
};

struct vertex_hash {
    size_t operator()(const LoadedObjModel::vertex_t& v) const noexcept {
        return (std::hash<glm::vec3>()(v.pos) ^ std::hash<glm::vec2>()(v.uv));
    }
};

LoadedObjModel::LoadedObjModel(const char* fname) {
    tinyobj_opt::attrib_t attrib;
    std::vector<tinyobj_opt::shape_t> shapes;
    std::vector<tinyobj_opt::material_t> materials;
    std::string err;

    
    {
        mango::filesystem::File model_file(fname);
        if (!model_file.data())
        {
            LOG(ERROR) << "Failed to find model file for ResourceContextScene test!";
            throw std::runtime_error("Failed to find model file for ResourceContextScene test!");
        }

        const mango::Memory model_memory{ model_file };

        tinyobj_opt::LoadOption loadOpts;
        loadOpts.req_num_threads = static_cast<int>(std::thread::hardware_concurrency() / 2);
        loadOpts.triangulate = true;

        if (!tinyobj_opt::parseObj(&attrib, &shapes, &materials, static_cast<char*>(model_memory), model_memory.size, loadOpts)) {
            LOG(ERROR) << "Internal error in tinyobj_opt, couldn't load model data! Error: " << err;
            throw std::runtime_error(err);
        }
    }

    std::unordered_map<vertex_t, uint32_t, vertex_hash> unique_vertices{};

    indices.reserve(attrib.indices.size());
    vertices.reserve(attrib.vertices.size());

    for (const auto& index : attrib.indices) {
        glm::vec3 pos{
            attrib.vertices[3 * index.vertex_index + 0],
            attrib.vertices[3 * index.vertex_index + 1],
            attrib.vertices[3 * index.vertex_index + 2]
        };

        glm::vec2 uv{
            attrib.texcoords[2 * index.texcoord_index + 0],
            1.0f - attrib.texcoords[2 * index.texcoord_index + 1]
        };

        vertex_t vert{ pos, uv };

        if (unique_vertices.count(vert) == 0) {
            unique_vertices[vert] = static_cast<uint32_t>(vertices.size());
            vertices.emplace_back(vert);
        }

        indices.emplace_back(unique_vertices[vert]);

    }

    vertices.shrink_to_fit();
    indices.shrink_to_fit();
}

VulkanComplexScene::VulkanComplexScene() : VulkanScene(), skyboxTextureReady(false), houseTextureReady(false), houseMeshReady(false) {}

VulkanComplexScene::~VulkanComplexScene() {
    Destroy();
}

VulkanComplexScene& VulkanComplexScene::GetScene() {
    static VulkanComplexScene scene;
    return scene;
}    

glm::vec3 scale(1.0f);

void VulkanComplexScene::Construct(RequiredVprObjects objects, void* user_data) {
    vprObjects = objects;
    resourceContext = &ResourceContext::Get();
    houseUboData.view = glm::lookAt(glm::vec3(-2.0f, -2.0f, 1.0f), glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    skyboxUboData.view = glm::mat3(houseUboData.view);
    houseUboData.projection = glm::perspectiveFov(glm::radians(70.0f), static_cast<float>(objects.swapchain->Extent().width), static_cast<float>(objects.swapchain->Extent().height), 0.1f, 1000.0f);
    houseUboData.projection[1][1] *= -1.0f;
    skyboxUboData.projection = houseUboData.projection;
    houseUboData.model = glm::mat4(1.0f);
    houseUboData.model = glm::scale(houseUboData.model, scale);
    skyboxUboData.model = glm::mat4(1.0f);
    sharedCache = std::make_unique<vpr::PipelineCache>(vprObjects.device->vkHandle(), vprObjects.physicalDevice->vkHandle(), typeid(VulkanComplexScene).hash_code());
    createSemaphores();
    createSampler();
    createUBOs();
    update();
    createFences();
    createCommandPool();
    createSkyboxMesh();
    createDescriptorPool();
    createShaders();
    createDescriptorSetLayouts();
    createDescriptorSets();
    createPipelineLayouts();
    depthStencil = CreateDepthStencil(vprObjects.device, vprObjects.physicalDevice, vprObjects.swapchain);
    createRenderpass();
    createFramebuffers();
    createHousePipeline();
    createSkyboxPipeline();
}

void VulkanComplexScene::Destroy() {

    const VkPipelineCache otherCaches[2]
    {
        houseCache->vkHandle(),
        skyboxCache->vkHandle()
    };

    sharedCache->MergeCaches(2u, otherCaches);

    auto destroy_rsrc_safe = [this](VulkanResource*& rsrc)
    {
        if (rsrc != nullptr)
        {
            resourceContext->DestroyResource(rsrc);
            rsrc = nullptr;
        }
    };

    destroy_rsrc_safe(sampler);
    destroy_rsrc_safe(houseEBO);
    destroy_rsrc_safe(houseVBO);
    destroy_rsrc_safe(houseUBO);
    destroy_rsrc_safe(houseTexture);
    destroy_rsrc_safe(skyboxEBO);
    destroy_rsrc_safe(skyboxVBO);
    destroy_rsrc_safe(skyboxUBO);
    destroy_rsrc_safe(skyboxTexture);
    houseSet.reset();
    skyboxSet.reset();
    pipelineLayout.reset();
    setLayout.reset();
    houseVert.reset();
    houseFrag.reset();
    skyboxVert.reset();
    skyboxFrag.reset();
    imageAcquireSemaphore.reset();
    renderCompleteSemaphore.reset();
    destroyFences();
    destroyFramebuffers();
    vkDestroyRenderPass(vprObjects.device->vkHandle(), renderPass, nullptr);
    vkFreeMemory(vprObjects.device->vkHandle(), depthStencil.Memory, nullptr);
    vkDestroyImageView(vprObjects.device->vkHandle(), depthStencil.View, nullptr);
    vkDestroyImage(vprObjects.device->vkHandle(), depthStencil.Image, nullptr);
    vkDestroyDescriptorUpdateTemplate(vprObjects.device->vkHandle(), houseTemplate, nullptr);
    vkDestroyDescriptorUpdateTemplate(vprObjects.device->vkHandle(), skyboxTemplate, nullptr);
    houseTextureReady = false;
    skyboxTextureReady = false;
    houseMeshReady = false;
}

void* VulkanComplexScene::LoadObjFile(const char* fname, void* user_data) {
    return new LoadedObjModel(fname);
}

void VulkanComplexScene::DestroyObjFileData(void* obj_file, void* user_data) {
    LoadedObjModel* model = reinterpret_cast<LoadedObjModel*>(obj_file);
    delete model;
}

void* VulkanComplexScene::LoadPngImage(const char* fname, void* user_data) {
    return new stb_image_data_t(fname);
}

void VulkanComplexScene::DestroyPngFileData(void * jpeg_file, void* user_data) {
    stb_image_data_t* image = reinterpret_cast<stb_image_data_t*>(jpeg_file);
    delete image;
}

void* VulkanComplexScene::LoadCompressedTexture(const char* fname, void* user_data) {
    return new gli::texture_cube(gli::load(fname));
}

void VulkanComplexScene::DestroyCompressedTextureData(void* compressed_texture, void* user_data) {
    gli::texture_cube* texture = reinterpret_cast<gli::texture_cube*>(compressed_texture);
    delete texture;
}

void VulkanComplexScene::CreateHouseMesh(void * obj_data) {
    LoadedObjModel* obj_model = reinterpret_cast<LoadedObjModel*>(obj_data);
    const auto device = vprObjects.device;

    const uint32_t queueFamilyIndices[2]{
        device->QueueFamilyIndices().Graphics,
        device->QueueFamilyIndices().Transfer
    };

    VkBufferCreateInfo vbo_info {
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        nullptr,
        0,
        static_cast<VkDeviceSize>(sizeof(LoadedObjModel::vertex_t) * obj_model->vertices.size()),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_SHARING_MODE_CONCURRENT,
        2u,
        queueFamilyIndices
    };

    // Test multi-copy functionality of resource context, by merging these two 
    // separate data buffers into a single VkBuffer
    const gpu_resource_data_t vbo_data {
        obj_model->vertices.data(),
        sizeof(LoadedObjModel::vertex_t) * obj_model->vertices.size(),
        0,
        VK_QUEUE_FAMILY_IGNORED
    };

    const VkBufferCreateInfo ebo_info{
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        nullptr,
        0,
        static_cast<VkDeviceSize>(sizeof(uint32_t) * obj_model->indices.size()),
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_SHARING_MODE_CONCURRENT,
        2u,
        queueFamilyIndices
    };

    const gpu_resource_data_t ebo_data{
        obj_model->indices.data(),
        static_cast<size_t>(ebo_info.size),
        0,
        VK_QUEUE_FAMILY_IGNORED
    };

    constexpr resource_creation_flags creationFlags{ ResourceCreateMemoryStrategyMinTime | ResourceCreateUserDataAsString };
    constexpr const char* houseVBO_Str{ "HouseVBO" };
    constexpr const char* houseEBO_Str{ "HouseEBO" };
    houseVBO = resourceContext->CreateBuffer(&vbo_info, nullptr, 1, &vbo_data, resource_usage::GPU_ONLY, creationFlags, (void*)houseVBO_Str);
    houseEBO = resourceContext->CreateBuffer(&ebo_info, nullptr, 1, &ebo_data, resource_usage::GPU_ONLY, creationFlags, (void*)houseEBO_Str);

    houseIndexCount = static_cast<uint32_t>(obj_model->indices.size());
    houseMeshReady = true;
}

void VulkanComplexScene::CreateHouseTexture(void * texture_data) {
    stb_image_data_t* image_data = reinterpret_cast<stb_image_data_t*>(texture_data);

    const uint32_t queueFamilyIndices[2]{
        vprObjects.device->QueueFamilyIndices().Graphics,
        vprObjects.device->QueueFamilyIndices().Transfer
    };

    const VkImageCreateInfo image_info{
        VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        nullptr,
        0,
        VK_IMAGE_TYPE_2D,
        VK_FORMAT_R8G8B8A8_UNORM,
        VkExtent3D{ static_cast<uint32_t>(image_data->width), static_cast<uint32_t>(image_data->height), 1 },
        1,
        1,
        VK_SAMPLE_COUNT_1_BIT,
        vprObjects.device->GetFormatTiling(VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT),
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        VK_SHARING_MODE_CONCURRENT,
        2u,
        queueFamilyIndices,
        VK_IMAGE_LAYOUT_UNDEFINED
    };

    const VkImageViewCreateInfo view_info{
        VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        nullptr,
        0,
        VK_NULL_HANDLE,
        VK_IMAGE_VIEW_TYPE_2D,
        VK_FORMAT_R8G8B8A8_UNORM,
        VkComponentMapping{ VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A },
        VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
    };

    const gpu_image_resource_data_t initial_texture_data[1] {
        gpu_image_resource_data_t {
            image_data->pixels,
            sizeof(stbi_uc) * image_data->width * image_data->height * image_data->channels,
            static_cast<uint32_t>(image_data->width), 
            static_cast<uint32_t>(image_data->height),
        }
    };

    constexpr resource_creation_flags creationFlags{ ResourceCreateMemoryStrategyMinTime | ResourceCreateUserDataAsString };
    constexpr const char* houseTextureStr{ "HouseTexture" };
    houseTexture = resourceContext->CreateImage(&image_info, &view_info, 1, initial_texture_data, resource_usage::GPU_ONLY, creationFlags, (void*)houseTextureStr);

    updateHouseDescriptorSet();
}

void VulkanComplexScene::CreateSkyboxTexture(void * texture_data) {

    LOG(INFO) << "Creating backing resources for loaded compressed skybox texture...";

    gli::texture_cube* texture = reinterpret_cast<gli::texture_cube*>(texture_data);
    const uint32_t width = static_cast<uint32_t>(texture->extent().x);
    const uint32_t height = static_cast<uint32_t>(texture->extent().y);
    const uint32_t mipLevels = static_cast<uint32_t>(texture->levels());

    const uint32_t queueFamilyIndices[2]{
        vprObjects.device->QueueFamilyIndices().Graphics,
        vprObjects.device->QueueFamilyIndices().Transfer
    };

    const VkImageCreateInfo image_info {
        VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        nullptr,
        VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT,
        VK_IMAGE_TYPE_2D,
        (VkFormat)texture->format(),
        VkExtent3D{ width, height, 1 },
        mipLevels,
        6,
        VK_SAMPLE_COUNT_1_BIT,
        vprObjects.device->GetFormatTiling((VkFormat)texture->format(), VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT),
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        VK_SHARING_MODE_CONCURRENT,
        2u,
        queueFamilyIndices,
        VK_IMAGE_LAYOUT_UNDEFINED
    };

    const VkImageViewCreateInfo view_info{
        VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        nullptr,
        0,
        VK_NULL_HANDLE,
        VK_IMAGE_VIEW_TYPE_CUBE,
        (VkFormat)texture->format(),
        VkComponentMapping{ VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A },
        VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels, 0, 6 }
    };

    const size_t total_size = texture->size();

    LOG(INFO) << "Creating image copy data, making sure layers and mips are correctly transferred...";
    std::vector<gpu_image_resource_data_t> image_copies;
    for (size_t i = 0; i < 6; ++i) {
        for (size_t j = 0; j < texture->levels(); ++j) {
            auto& ref = *texture;
            image_copies.emplace_back(gpu_image_resource_data_t{
                ref[i][j].data(),
                ref[i][j].size(),
                static_cast<uint32_t>(ref[i][j].extent().x),
                static_cast<uint32_t>(ref[i][j].extent().y),
                static_cast<uint32_t>(i),
                1u,
                static_cast<uint32_t>(j),
                queueFamilyIndices[0]
            });
        }
    }

    constexpr resource_creation_flags creationFlags{ ResourceCreateMemoryStrategyMinTime | ResourceCreateUserDataAsString };
    constexpr const char* skyboxTextureStr{ "SkyboxTexture" };
    skyboxTexture = resourceContext->CreateImage(&image_info, &view_info, image_copies.size(), image_copies.data(), resource_usage::GPU_ONLY, creationFlags, (void*)skyboxTextureStr);
    ResourceContext::Get().Update();
    updateSkyboxDescriptorSet();

}

bool VulkanComplexScene::AllAssetsLoaded() {
    return (skyboxTextureReady && houseMeshReady && houseTextureReady);
}

void VulkanComplexScene::WaitForAllLoaded() {
    while (!skyboxTextureReady || !houseMeshReady || !houseTextureReady) {
        
    }
    resourceContext->Update();
    std::cerr << "All data loaded.";
}

const glm::vec3 translation(0.0f, 0.0f, 0.0f);

void VulkanComplexScene::update() {
    static auto start_time = std::chrono::high_resolution_clock::now();
    auto curr_time = std::chrono::high_resolution_clock::now();
    float diff = static_cast<float>(std::chrono::duration_cast<std::chrono::milliseconds>(curr_time - start_time).count()) / 10000.0f;
    houseUboData.model = glm::scale(glm::mat4(1.0f), scale);
    houseUboData.model = glm::rotate(houseUboData.model, diff * glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f)); // pivot house around center axis based on time.
    houseUboData.model = glm::translate(houseUboData.model, translation);
    const gpu_resource_data_t house_ubo_data{
        &houseUboData,
        sizeof(ubo_data_t),
        0,
        VK_QUEUE_FAMILY_IGNORED
    };
    resourceContext->SetBufferData(houseUBO, 1, &house_ubo_data); 
    //skyboxUboData.model = glm::rotate(glm::mat4(1.0f), diff * glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    const gpu_resource_data_t skybox_ubo_data{
        &skyboxUboData,
        sizeof(ubo_data_t),
        0,
        VK_QUEUE_FAMILY_IGNORED
    };
    resourceContext->SetBufferData(skyboxUBO, 1, &skybox_ubo_data);
}

void VulkanComplexScene::recordCommands() {

    static std::array<bool, 5> first_frame{ true, true, true, true, true };

    if (!first_frame[currentBuffer]) {
        cmdPool->ResetCmdBuffer(currentBuffer);
    }

    constexpr static VkCommandBufferBeginInfo begin_info{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        nullptr,
        VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT | VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT,
        nullptr
    };

    constexpr static std::array<VkClearValue, 2> clearValues{
        VkClearValue{ VkClearColorValue{ 0.0f, 0.0f, 0.5f, 1.0f } },
        VkClearValue{ 1.0f, 0 }
    };

    const VkRect2D render_area{
        VkOffset2D{ 0, 0 },
        VkExtent2D{ vprObjects.swapchain->Extent() }
    };

    const VkViewport viewport{
        0.0f,
        0.0f,
        static_cast<float>(vprObjects.swapchain->Extent().width),
        static_cast<float>(vprObjects.swapchain->Extent().height),
        0.0f,
        1.0f
    };

    const VkRect2D scissor{
        render_area
    };

    VkRenderPassBeginInfo rpBegin{
        VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        nullptr,
        renderPass,
        VK_NULL_HANDLE,
        render_area,
        static_cast<uint32_t>(clearValues.size()),
        clearValues.data()
    };

    {
        rpBegin.framebuffer = framebuffers[currentBuffer];
        VkResult result = VK_SUCCESS;
        auto& pool = *cmdPool;
        result = vkBeginCommandBuffer(pool[currentBuffer], &begin_info); VkAssert(result);
            vkCmdBeginRenderPass(pool[currentBuffer], &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
            if (skyboxTextureReady) {
                vkCmdSetViewport(pool[currentBuffer], 0, 1, &viewport);
                vkCmdSetScissor(pool[currentBuffer], 0, 1, &scissor);
                renderSkybox(pool[currentBuffer]);
            }
            if (houseTextureReady && houseMeshReady) {
                vkCmdSetViewport(pool[currentBuffer], 0, 1, &viewport);
                vkCmdSetScissor(pool[currentBuffer], 0, 1, &scissor);
                renderHouse(pool[currentBuffer]);
            }
            vkCmdEndRenderPass(pool[currentBuffer]);
        result = vkEndCommandBuffer(pool[currentBuffer]);
        VkAssert(result);
    }

    first_frame[currentBuffer] = false;
}

void VulkanComplexScene::renderHouse(VkCommandBuffer cmd) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, housePipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout->vkHandle(), 0, 1, &houseSet->vkHandle(), 0, nullptr);
    const VkBuffer buffers[1]{ (VkBuffer)houseVBO->Handle };
    constexpr static VkDeviceSize offsets[1]{ 0 };
    vkCmdBindVertexBuffers(cmd, 0, 1, buffers, offsets);
    vkCmdBindIndexBuffer(cmd, (VkBuffer)houseEBO->Handle, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, houseIndexCount, 1, 0, 0, 0);
}

void VulkanComplexScene::renderSkybox(VkCommandBuffer cmd) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, skyboxPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout->vkHandle(), 0, 1, &skyboxSet->vkHandle(), 0, nullptr);
    const VkBuffer buffer[1]{ (VkBuffer)skyboxVBO->Handle };
    constexpr static VkDeviceSize offsets[1]{ 0 };
    vkCmdBindVertexBuffers(cmd, 0, 1, buffer, offsets);
    vkCmdBindIndexBuffer(cmd, (VkBuffer)skyboxEBO->Handle, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, skyboxIndexCount, 1, 0, 0, 0);
}

void VulkanComplexScene::draw() {

    constexpr static VkPipelineStageFlags wait_mask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    const VkSubmitInfo submit_info{
        VK_STRUCTURE_TYPE_SUBMIT_INFO,
        nullptr,
        1,
        &imageAcquireSemaphore->vkHandle(),
        &wait_mask,
        1,
        &cmdPool->GetCmdBuffer(currentBuffer),
        1,
        &renderCompleteSemaphore->vkHandle()
    };

    VkResult result = vkQueueSubmit(vprObjects.device->GraphicsQueue(), 1, &submit_info, fences[currentBuffer]);
    VkAssert(result);
}

void VulkanComplexScene::endFrame() {

    VkResult result = vkWaitForFences(vprObjects.device->vkHandle(), 1, &fences[currentBuffer], VK_TRUE, UINT64_MAX);
    VkAssert(result);
    result = vkResetFences(vprObjects.device->vkHandle(), 1, &fences[currentBuffer]);
    VkAssert(result);

}

void VulkanComplexScene::createSampler() {
    constexpr static VkSamplerCreateInfo sampler_info{
        VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        nullptr,
        0,
        VK_FILTER_LINEAR,
        VK_FILTER_LINEAR,
        VK_SAMPLER_MIPMAP_MODE_LINEAR,
        VK_SAMPLER_ADDRESS_MODE_REPEAT,
        VK_SAMPLER_ADDRESS_MODE_REPEAT,
        VK_SAMPLER_ADDRESS_MODE_REPEAT,
        1.0f,
        VK_TRUE,
        4.0f,
        VK_FALSE,
        VK_COMPARE_OP_BEGIN_RANGE,
        0.0f,
        3.0f,
        VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
        VK_FALSE
    };
    constexpr const char* samplerStr{ "SharedSampler" };
    sampler = resourceContext->CreateSampler(&sampler_info, ResourceCreateUserDataAsString, (void*)samplerStr);
}

void VulkanComplexScene::createUBOs() {
    constexpr static VkBufferCreateInfo ubo_info{
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        nullptr,
        0,
        sizeof(ubo_data_t),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_SHARING_MODE_EXCLUSIVE,
        0,
        nullptr
    };
    constexpr resource_creation_flags uboFlags{ ResourceCreateMemoryStrategyMinFragmentation | ResourceCreateUserDataAsString | ResourceCreatePersistentlyMapped };
    constexpr const char* houseUboStr{ "HouseUBO" };
    constexpr const char* skyboxUboStr{ "SkyboxUBO" };
    houseUBO = resourceContext->CreateBuffer(&ubo_info, nullptr, 0, nullptr, resource_usage::CPU_ONLY, uboFlags, (void*)houseUboStr);
    skyboxUBO = resourceContext->CreateBuffer(&ubo_info, nullptr, 0, nullptr, resource_usage::CPU_ONLY, uboFlags, (void*)skyboxUboStr);
}

void VulkanComplexScene::createSkyboxMesh() {
    skybox_mesh_data_t mesh_data;

    const uint32_t queueFamilyIndices[2]{
        vprObjects.device->QueueFamilyIndices().Graphics,
        vprObjects.device->QueueFamilyIndices().Transfer
    };

    const VkBufferCreateInfo vbo_info{
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        nullptr,
        0,
        static_cast<VkDeviceSize>(sizeof(glm::vec3) * mesh_data.vertices.size()),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_SHARING_MODE_CONCURRENT,
        2u,
        queueFamilyIndices
    };

    const VkBufferCreateInfo ebo_info{
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        nullptr,
        0,
        static_cast<VkDeviceSize>(sizeof(uint32_t) * mesh_data.indices.size()),
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_SHARING_MODE_CONCURRENT,
        2u,
        queueFamilyIndices
    };

    const gpu_resource_data_t vbo_data{
        mesh_data.vertices.data(),
        vbo_info.size,
        0,
        VK_QUEUE_FAMILY_IGNORED
    };

    const gpu_resource_data_t ebo_data{
        mesh_data.indices.data(),
        ebo_info.size,
        0,
        VK_QUEUE_FAMILY_IGNORED
    };

    constexpr resource_creation_flags creationFlags{ ResourceCreateMemoryStrategyMinTime | ResourceCreateUserDataAsString };
    constexpr const char* skyboxVboStr{ "SkyboxVBO" };
    constexpr const char* skyboxEboStr{ "SkyboxEBO" };
    skyboxVBO = resourceContext->CreateBuffer(&vbo_info, nullptr, 1, &vbo_data, resource_usage::GPU_ONLY, creationFlags, (void*)skyboxVboStr);
    skyboxEBO = resourceContext->CreateBuffer(&ebo_info, nullptr, 1, &ebo_data, resource_usage::GPU_ONLY, creationFlags, (void*)skyboxEboStr);
    skyboxIndexCount = static_cast<uint32_t>(mesh_data.indices.size());
}

void VulkanComplexScene::createFences() {
    constexpr static VkFenceCreateInfo fence_info {
        VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        nullptr,
        0
    };
    fences.resize(vprObjects.swapchain->ImageCount());
    for (auto& fence : fences) {
        VkResult result = vkCreateFence(vprObjects.device->vkHandle(), &fence_info, nullptr, &fence);
        VkAssert(result);
    }
}

void VulkanComplexScene::createCommandPool() {
    const VkCommandPoolCreateInfo create_info{
        VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        nullptr,
        VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT | VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
        vprObjects.device->QueueFamilyIndices().Graphics
    };
    cmdPool = std::make_unique<vpr::CommandPool>(vprObjects.device->vkHandle(), create_info);
    cmdPool->AllocateCmdBuffers(vprObjects.swapchain->ImageCount(), VK_COMMAND_BUFFER_LEVEL_PRIMARY);
}

void VulkanComplexScene::createDescriptorPool() {
    descriptorPool = std::make_unique<vpr::DescriptorPool>(vprObjects.device->vkHandle(), 3);
    descriptorPool->AddResourceType(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2);
    descriptorPool->AddResourceType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2);
    descriptorPool->Create();
}

void VulkanComplexScene::createDescriptorSetLayouts() {

    constexpr static VkDescriptorSetLayoutBinding unique_bindings[2] { 
        VkDescriptorSetLayoutBinding{
            0,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            1,
            VK_SHADER_STAGE_VERTEX_BIT,
            nullptr
        },
        VkDescriptorSetLayoutBinding{
            1,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            1,
            VK_SHADER_STAGE_FRAGMENT_BIT,
            nullptr
        }
    };

    setLayout = std::make_unique<vpr::DescriptorSetLayout>(vprObjects.device->vkHandle());
    setLayout->AddDescriptorBindings(2, unique_bindings);

}

void VulkanComplexScene::createDescriptorSets() {
    houseSet = std::make_unique<vpr::DescriptorSet>(vprObjects.device->vkHandle());
    skyboxSet = std::make_unique<vpr::DescriptorSet>(vprObjects.device->vkHandle());
    houseSet->AddDescriptorInfo(VkDescriptorBufferInfo{ (VkBuffer)houseUBO->Handle, 0, sizeof(ubo_data_t) }, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0);
    skyboxSet->AddDescriptorInfo(VkDescriptorBufferInfo{ (VkBuffer)skyboxUBO->Handle, 0, sizeof(ubo_data_t) }, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0);
}

void VulkanComplexScene::createUpdateTemplates() {
    const VkDescriptorUpdateTemplateCreateInfo house_info{
        VK_STRUCTURE_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_CREATE_INFO,
        nullptr,
        0,
        1,
        nullptr,
        VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_DESCRIPTOR_SET,
        setLayout->vkHandle(),
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        VK_NULL_HANDLE,

    };
}

void VulkanComplexScene::createPipelineLayouts() {  
    const VkDescriptorSetLayout set_layouts[1]{ setLayout->vkHandle() };
    pipelineLayout = std::make_unique<vpr::PipelineLayout>(vprObjects.device->vkHandle());
    pipelineLayout->Create(1, set_layouts);
}

void VulkanComplexScene::createShaders() {
    houseVert = std::make_unique<vpr::ShaderModule>(vprObjects.device->vkHandle(), VK_SHADER_STAGE_VERTEX_BIT, house_shader_vert_spv, static_cast<uint32_t>(sizeof(house_shader_vert_spv)));
    houseFrag = std::make_unique<vpr::ShaderModule>(vprObjects.device->vkHandle(), VK_SHADER_STAGE_FRAGMENT_BIT, house_shader_frag_spv, static_cast<uint32_t>(sizeof(house_shader_frag_spv)));
    skyboxVert = std::make_unique<vpr::ShaderModule>(vprObjects.device->vkHandle(), VK_SHADER_STAGE_VERTEX_BIT, skybox_shader_vert_spv, static_cast<uint32_t>(sizeof(skybox_shader_vert_spv)));
    skyboxFrag = std::make_unique<vpr::ShaderModule>(vprObjects.device->vkHandle(), VK_SHADER_STAGE_FRAGMENT_BIT, skybox_shader_frag_spv, static_cast<uint32_t>(sizeof(skybox_shader_frag_spv)));
}

void VulkanComplexScene::createFramebuffers() {
    framebuffers.resize(vprObjects.swapchain->ImageCount());
    for (size_t i = 0; i < framebuffers.size(); ++i) {
        std::array<VkImageView, 2> views{
            vprObjects.swapchain->ImageView(i),
            depthStencil.View
        };

        const VkFramebufferCreateInfo create_info{
            VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            nullptr,
            0,
            renderPass,
            static_cast<uint32_t>(views.size()),
            views.data(),
            vprObjects.swapchain->Extent().width,
            vprObjects.swapchain->Extent().height,
            1
        };

        VkResult result = vkCreateFramebuffer(vprObjects.device->vkHandle(), &create_info, nullptr, &framebuffers[i]);
        VkAssert(result);
    }
}

void VulkanComplexScene::createRenderpass() {
    renderPass = CreateBasicRenderpass(vprObjects.device, vprObjects.swapchain, depthStencil.Format);
}

void VulkanComplexScene::createHousePipeline() {

    const VkPipelineShaderStageCreateInfo shader_stages[2]{
        houseVert->PipelineInfo(),
        houseFrag->PipelineInfo()
    };

    constexpr static VkVertexInputBindingDescription vertex_bindings[1] {
        VkVertexInputBindingDescription{ 0, sizeof(LoadedObjModel::vertex_t), VK_VERTEX_INPUT_RATE_VERTEX },
    };

    constexpr static VkVertexInputAttributeDescription vertex_attrs[2] {
        VkVertexInputAttributeDescription{ 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 },
        VkVertexInputAttributeDescription{ 1, 0, VK_FORMAT_R32G32_SFLOAT, sizeof(glm::vec3) }
    };

    constexpr static VkPipelineVertexInputStateCreateInfo vertex_info{
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        nullptr,
        0,
        1,
        vertex_bindings,
        2,
        vertex_attrs
    };

    houseCache = std::make_unique<vpr::PipelineCache>(vprObjects.device->vkHandle(), vprObjects.physicalDevice->vkHandle(), sharedCache->vkHandle(), typeid(VulkanComplexScene).hash_code() + std::hash<std::string>()("HouseCache"));
    housePipeline = CreateBasicPipeline(vprObjects.device, 2, shader_stages, &vertex_info, pipelineLayout->vkHandle(), renderPass, VK_COMPARE_OP_LESS, houseCache->vkHandle(), VK_NULL_HANDLE, VK_CULL_MODE_BACK_BIT);

}

void VulkanComplexScene::createSkyboxPipeline() {

    const VkPipelineShaderStageCreateInfo shader_stages[2]{
        skyboxVert->PipelineInfo(),
        skyboxFrag->PipelineInfo()
    };

    constexpr static VkVertexInputBindingDescription vertex_bindings[1] {
        VkVertexInputBindingDescription{ 0, sizeof(glm::vec3), VK_VERTEX_INPUT_RATE_VERTEX }
    };

    constexpr static VkVertexInputAttributeDescription vertex_attr[1] {
        VkVertexInputAttributeDescription{ 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 }
    };

    constexpr static VkPipelineVertexInputStateCreateInfo vertex_info {
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        nullptr,
        0,
        1,
        vertex_bindings,
        1,
        vertex_attr
    };

    skyboxCache = std::make_unique<vpr::PipelineCache>(vprObjects.device->vkHandle(), vprObjects.physicalDevice->vkHandle(), sharedCache->vkHandle(), typeid(VulkanComplexScene).hash_code() + std::hash<std::string>()("SkyboxCache"));
    skyboxPipeline = CreateBasicPipeline(vprObjects.device, 2, shader_stages, &vertex_info, pipelineLayout->vkHandle(), renderPass, VK_COMPARE_OP_LESS_OR_EQUAL, skyboxCache->vkHandle(), housePipeline);

}

void VulkanComplexScene::destroyFences() {
    for (auto& fence : fences) {
        vkDestroyFence(vprObjects.device->vkHandle(), fence, nullptr);
    }
}

void VulkanComplexScene::destroyFramebuffers() {
    for (auto& fbuff : framebuffers) {
        vkDestroyFramebuffer(vprObjects.device->vkHandle(), fbuff, nullptr);
    }
}

void VulkanComplexScene::updateHouseDescriptorSet() {
    houseSet->AddDescriptorInfo(VkDescriptorImageInfo{ (VkSampler)sampler->Handle, (VkImageView)houseTexture->ViewHandle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1);
    houseSet->Init(descriptorPool->vkHandle(), setLayout->vkHandle());
    houseTextureReady = true;
}

void VulkanComplexScene::updateSkyboxDescriptorSet() {
    skyboxSet->AddDescriptorInfo(VkDescriptorImageInfo{ (VkSampler)sampler->Handle, (VkImageView)skyboxTexture->ViewHandle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1);
    skyboxSet->Init(descriptorPool->vkHandle(), setLayout->vkHandle());
    skyboxTextureReady = true;
}
