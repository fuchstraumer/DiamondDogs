#include "ComplexScene.hpp"
#include "PipelineCache.hpp"
#include "DescriptorPool.hpp"
#include "DescriptorSet.hpp"
#include "DescriptorSetLayout.hpp"
#include "Fence.hpp"
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
#include "PipelineExecutableInfo.hpp"
#include "ResourceMessageReply.hpp"
#include "MathHashes.hpp"
#pragma warning(push, 1)
#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"
#define TINYOBJLOADER_IMPLEMENTATION 
#include <tinyobjloader/tiny_obj_loader.h>
#pragma warning(pop)

#include "../../RenderingContext/include/RenderingContext.hpp"

#include "gli/gli.hpp"

#include <iostream>
#include <array>
#include <fstream>
#include <unordered_map>
#include <numbers>
#include <format>

const static std::array<Float3, 8> skybox_positions
{
    Float3(-1.0f,-1.0f, 1.0f ),
    Float3( 1.0f,-1.0f, 1.0f ),
    Float3( 1.0f, 1.0f, 1.0f ),
    Float3(-1.0f, 1.0f, 1.0f ),
    Float3( 1.0f,-1.0f,-1.0f ),
    Float3(-1.0f,-1.0f,-1.0f ),
    Float3(-1.0f, 1.0f,-1.0f ),
    Float3( 1.0f, 1.0f,-1.0f )
};

#ifndef _NDEBUG
static PipelineExecutableInfo* houseExecutableInfo = nullptr;
static PipelineExecutableInfo* skyboxExecutableInfo = nullptr;
constexpr VkPipelineCreateFlags pipelineFlags{ VK_PIPELINE_CREATE_CAPTURE_STATISTICS_BIT_KHR };
#else
constexpr static PipelineExecutableInfo* houseExecutableInfo = nullptr;
constexpr static PipelineExecutableInfo* skyboxExecutableInfo = nullptr;
constexpr VkPipelineCreateFlags pipelineFlags{ 0 };
#endif


struct skybox_mesh_data_t
{

    skybox_mesh_data_t()
    {
        auto add_vertex = [&](const Float3& v)->uint32_t
        {
            vertices.emplace_back(v);
            return static_cast<uint32_t>(vertices.size() - 1);
        };

        auto build_face = [&](const Float3& v0, const Float3& v1, const Float3& v2, const Float3& v3)
        {
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

    std::vector<Float3> vertices;
    std::vector<uint32_t> indices;
};

struct stb_image_data_t
{

    stb_image_data_t(const char* fname)
    {
        pixels = stbi_load(fname, &width, &height, &channels, 4);
        if (!pixels)
        {
            throw std::runtime_error("Invalid file path for stb_load");
        }
    }

    ~stb_image_data_t()
    {
        if (pixels)
        {
            stbi_image_free(pixels);
        }
        pixels = nullptr;
    }
    stbi_uc* pixels = nullptr;
    int width = -1;
    int height = -1;
    int channels = -1;
};

struct vertex_hash
{
    size_t operator()(const LoadedObjModel::vertex_t& v) const noexcept
    {
        return (std::hash<Float3>()(v.pos) ^ std::hash<Float2>()(v.uv));
    }
};

LoadedObjModel::LoadedObjModel(const char* fname)
{
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string err;
    std::string warnings;
    
    {
        if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warnings, &err, fname))
        {
            std::cerr << "Internal error in tinyobj_opt, couldn't load model data! Error: " << err;
            throw std::runtime_error(err);
        }
    }

    std::unordered_map<vertex_t, uint32_t, vertex_hash> unique_vertices{};

    // Pre-allocate with a reasonable size
    vertices.reserve(attrib.vertices.size() / 3);
    
    // Process all shapes
    for (const auto& shape : shapes)
    {
        // Reserve space for indices
        size_t index_offset = 0;
        
        // For each face
        for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); f++)
        {
            // For each vertex in the face
            for (size_t v = 0; v < shape.mesh.num_face_vertices[f]; v++)
            {
                // Access index
                tinyobj::index_t idx = shape.mesh.indices[index_offset + v];
                
                // Get vertex position
                Float3 pos
                {
                    attrib.vertices[3 * idx.vertex_index + 0],
                    attrib.vertices[3 * idx.vertex_index + 1],
                    attrib.vertices[3 * idx.vertex_index + 2]
                };

                // Get texture coordinates if available
                Float2 uv{0.0f, 0.0f};
                if (idx.texcoord_index >= 0)
                {
                    uv.x = attrib.texcoords[2 * idx.texcoord_index + 0];
                    uv.y = 1.0f - attrib.texcoords[2 * idx.texcoord_index + 1];
                }

                vertex_t vert{ pos, uv };

                if (unique_vertices.count(vert) == 0)
                {
                    unique_vertices[vert] = static_cast<uint32_t>(vertices.size());
                    vertices.emplace_back(vert);
                }

                indices.emplace_back(unique_vertices[vert]);
            }
            
            index_offset += shape.mesh.num_face_vertices[f];
        }
    }

    vertices.shrink_to_fit();
    indices.shrink_to_fit();
}

VulkanComplexScene::VulkanComplexScene() : VulkanScene() {}

VulkanComplexScene::~VulkanComplexScene()
{
    Destroy();
}

VulkanComplexScene& VulkanComplexScene::GetScene()
{
    static VulkanComplexScene scene;
    return scene;
}    

Float3 scale(1.0f);

void VulkanComplexScene::Construct(RequiredVprObjects objects, void* user_data)
{
    vprObjects = objects;
    numFramebuffers = objects.swapchain->ImageCount();
    resourceContext = reinterpret_cast<ResourceContext*>(user_data);

    Matrix viewMatrix = Matrix::LookAt(Vector(-2.0f, -2.0f, 1.0f), Vector(0.0f), Vector(0.0f, 0.0f, 1.0f));
    houseUboData.view = FromMatrix<Float4x4>(viewMatrix);
    skyboxUboData.view = Float4x4(FromMatrix<Float3x3>(viewMatrix));
    const float aspectRatio = static_cast<float>(objects.swapchain->Extent().width) / static_cast<float>(objects.swapchain->Extent().height);
    Matrix projectionMatrix = Matrix::PerspectiveRH(
        70.0f,
        aspectRatio,
        0.1f,
        1000.0f);

    houseUboData.projection = FromMatrix<Float4x4>(projectionMatrix);
    houseUboData.projection(1,1) *= -1.0f; // Vulkan uses a different coordinate system for Y
    skyboxUboData.projection = houseUboData.projection;

    Matrix houseModelMatrix = Matrix::Identity() * Matrix::Scale(Vector(scale.x, scale.y, scale.z));
    houseUboData.model = FromMatrix<Float4x4>(houseModelMatrix);
    skyboxUboData.model = FromMatrix<Float4x4>(houseModelMatrix);

    sharedCache = std::make_unique<vpr::PipelineCache>(vprObjects.device->vkHandle(), vprObjects.physicalDevice->vkHandle(), typeid(VulkanComplexScene).hash_code());
    createFrameSyncObjects();
    setupSwapchainDebugInfo();
    createSampler();
    createUBOs();
    update();
    createCommandPool();
    createSkyboxMesh();
    createDescriptorPool();
    createShaders();
    createDescriptorSetLayouts();
    createDescriptorSets();
    createPipelineLayouts();
    for (uint32_t i = 0; i < numFramebuffers; ++i)
    {
        depthStencils.emplace_back(
            vprObjects.device, 
            vprObjects.physicalDevice, 
            vprObjects.swapchain);

        auto& ds = depthStencils.back();
        std::string dsName = std::format("DepthStencilImage[{}]", i);
        RenderingContext::SetObjectName(
            VK_OBJECT_TYPE_IMAGE,
            reinterpret_cast<uint64_t>(ds.Image),
            RENDERING_CONTEXT_DEBUG_OBJECT_NAME(dsName.c_str()));
        dsName = std::format("DepthStencilImageView[{}]", i);
        RenderingContext::SetObjectName(
            VK_OBJECT_TYPE_IMAGE_VIEW,
            reinterpret_cast<uint64_t>(ds.View),
            RENDERING_CONTEXT_DEBUG_OBJECT_NAME(dsName.c_str()));
        dsName = std::format("DepthStencilImageMemory[{}]", i);
        RenderingContext::SetObjectName(
            VK_OBJECT_TYPE_DEVICE_MEMORY,
            reinterpret_cast<uint64_t>(ds.Memory),
            RENDERING_CONTEXT_DEBUG_OBJECT_NAME(dsName.c_str()));
    }
    createHousePipeline();
    createSkyboxPipeline();
}

void VulkanComplexScene::Destroy()
{

    const VkPipelineCache otherCaches[2]
    {
        houseCache->vkHandle(),
        skyboxCache->vkHandle()
    };

    sharedCache->MergeCaches(2u, otherCaches);

    auto destroyResource = 
        [this](GraphicsResource rsrc)
        {
            auto deletionReply = resourceContext->DestroyResource(rsrc);
            MessageReply::Status status = deletionReply->WaitForCompletion();
            assert(status == MessageReply::Status::Completed);
        };
  
    destroyResource(houseTexture);
    destroyResource(skyboxTexture);
    destroyResource(houseVBO);
    destroyResource(houseEBO);
    destroyResource(skyboxVBO);
    destroyResource(skyboxEBO);
    destroyResource(houseUBO);
    destroyResource(skyboxUBO);
    destroyResource(sampler);
    houseSet.reset();
    skyboxSet.reset();
    pipelineLayout.reset();
    setLayout.reset();
    houseVert.reset();
    houseFrag.reset();
    skyboxVert.reset();
    skyboxFrag.reset();

    for (auto& depthStencil : depthStencils)
    {
        vkFreeMemory(vprObjects.device->vkHandle(), depthStencil.Memory, nullptr);
        vkDestroyImageView(vprObjects.device->vkHandle(), depthStencil.View, nullptr);
        vkDestroyImage(vprObjects.device->vkHandle(), depthStencil.Image, nullptr);
    }
    depthStencils.clear();
    vkDestroyDescriptorUpdateTemplate(vprObjects.device->vkHandle(), houseTemplate, nullptr);
    vkDestroyDescriptorUpdateTemplate(vprObjects.device->vkHandle(), skyboxTemplate, nullptr);
    houseTextureReply.reset();
    skyboxTextureReply.reset();
    houseVboReply.reset();
    houseEboReply.reset();
    skyboxVboReply.reset();
    skyboxEboReply.reset();

    destroyFrameSyncObjects();
}

void* VulkanComplexScene::LoadObjFile(const char* fname, void* user_data)
{
    return new LoadedObjModel(fname);
}

void VulkanComplexScene::DestroyObjFileData(void* obj_file, void* user_data)
{
    LoadedObjModel* model = reinterpret_cast<LoadedObjModel*>(obj_file);
    delete model;
}

void* VulkanComplexScene::LoadPngImage(const char* fname, void* user_data)
{
    return new stb_image_data_t(fname);
}

void VulkanComplexScene::DestroyPngFileData(void * jpeg_file, void* user_data)
{
    stb_image_data_t* image = reinterpret_cast<stb_image_data_t*>(jpeg_file);
    delete image;
}

void* VulkanComplexScene::LoadCompressedTexture(const char* fname, void* user_data)
{
    return new gli::texture_cube(gli::load(fname));
}

void VulkanComplexScene::DestroyCompressedTextureData(void* compressed_texture, void* user_data)
{
    gli::texture_cube* texture = reinterpret_cast<gli::texture_cube*>(compressed_texture);
    delete texture;
}

void VulkanComplexScene::CreateHouseMesh(void* obj_data)
{
    LoadedObjModel* obj_model = reinterpret_cast<LoadedObjModel*>(obj_data);
    const auto device = vprObjects.device;

    const uint32_t queueFamilyIndices[2]
    {
        device->QueueFamilyIndices().Graphics,
        device->QueueFamilyIndices().Transfer
    };

    VkBufferCreateInfo vbo_info
    {
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
    const RhiBufferResourceData vbo_data
    {
        obj_model->vertices.data(),
        sizeof(LoadedObjModel::vertex_t) * obj_model->vertices.size(),
        0,
        0
    };

    const VkBufferCreateInfo ebo_info
    {
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        nullptr,
        0,
        static_cast<VkDeviceSize>(sizeof(uint32_t) * obj_model->indices.size()),
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_SHARING_MODE_CONCURRENT,
        2u,
        queueFamilyIndices
    };

    const RhiBufferResourceData ebo_data
    {
        obj_model->indices.data(),
        static_cast<size_t>(ebo_info.size),
        0,
        0
    };

    constexpr resource_creation_flags creationFlags{ resource_creation_flag_bits::UserDataAsString };
    constexpr const char* houseVBO_Str{ "HouseVBO" };
    constexpr const char* houseEBO_Str{ "HouseEBO" };
    
    VkBufferCreateInfo vbo_info_val = vbo_info;
    VkBufferCreateInfo ebo_info_val = ebo_info;
    
    houseVboReply = resourceContext->CreateBuffer(
        vbo_info_val, 
        nullptr, 
        &vbo_data, 
        1, 
        resource_usage::GPUOnly, 
        creationFlags, 
        (void*)houseVBO_Str);
    
    houseEboReply = resourceContext->CreateBuffer(
        ebo_info_val, 
        nullptr, 
        &ebo_data, 
        1, 
        resource_usage::GPUOnly, 
        creationFlags, 
        (void*)houseEBO_Str);

    houseIndexCount = static_cast<uint32_t>(obj_model->indices.size());

}

void VulkanComplexScene::CreateHouseTexture(void * texture_data)
{
    stb_image_data_t* image_data = reinterpret_cast<stb_image_data_t*>(texture_data);

    const uint32_t queueFamilyIndices[2]
    {
        vprObjects.device->QueueFamilyIndices().Graphics,
        vprObjects.device->QueueFamilyIndices().Transfer
    };

    const VkImageCreateInfo image_info
    {
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

    const VkImageViewCreateInfo view_info
    {
        VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        nullptr,
        0,
        VK_NULL_HANDLE,
        VK_IMAGE_VIEW_TYPE_2D,
        VK_FORMAT_R8G8B8A8_UNORM,
        VkComponentMapping{ VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A },
        VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
    };

    const RhiImageResourceData initial_texture_data[1]
    {
        RhiImageResourceData
        {
            reinterpret_cast<const void*>(image_data->pixels),
            static_cast<size_t>(sizeof(stbi_uc) * image_data->width * image_data->height * image_data->channels),
            static_cast<uint32_t>(image_data->width), 
            static_cast<uint32_t>(image_data->height),
            0,
            1,
            0,
            queue_family_flag_bits::Ignored
        }
    };

    constexpr resource_creation_flags creationFlags{ resource_creation_flag_bits::UserDataAsString };
    constexpr const char* houseTextureStr{ "HouseTexture" };
    
    VkImageCreateInfo image_info_val = image_info;
    VkImageViewCreateInfo view_info_val = view_info;
    
    houseTextureReply = resourceContext->CreateImage(
        image_info_val, 
        &view_info_val, 
        initial_texture_data, 
        1, 
        resource_usage::GPUOnly, 
        creationFlags, 
        (void*)houseTextureStr);

}

void VulkanComplexScene::CreateSkyboxTexture(void* texture_data)
{

    std::cout << "Creating backing resources for loaded compressed skybox texture...\n";

    gli::texture_cube* texture = reinterpret_cast<gli::texture_cube*>(texture_data);
    const uint32_t width = static_cast<uint32_t>(texture->extent().x);
    const uint32_t height = static_cast<uint32_t>(texture->extent().y);
    const uint32_t mipLevels = static_cast<uint32_t>(texture->levels());

    const uint32_t queueFamilyIndices[2]
    {
        vprObjects.device->QueueFamilyIndices().Graphics,
        vprObjects.device->QueueFamilyIndices().Transfer
    };

    const VkImageCreateInfo image_info
    {
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

    const VkImageViewCreateInfo view_info
    {
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

    std::cout << "Creating image copy data, making sure layers and mips are correctly transferred...\n";
    std::vector<RhiImageResourceData> image_copies;
    for (size_t i = 0; i < 6; ++i)
    {
        for (size_t j = 0; j < texture->levels(); ++j)
        {
            auto& ref = *texture;
            image_copies.emplace_back(RhiImageResourceData
            {
                ref[i][j].data(),
                ref[i][j].size(),
                static_cast<uint32_t>(ref[i][j].extent().x),
                static_cast<uint32_t>(ref[i][j].extent().y),
                static_cast<uint32_t>(i),
                1u,
                static_cast<uint32_t>(j),
                queue_family_flag_bits::Graphics | queue_family_flag_bits::Transfer
            });
        }
    }

    constexpr resource_creation_flags creationFlags{ resource_creation_flag_bits::UserDataAsString };
    constexpr const char* skyboxTextureStr{ "SkyboxTexture" };
    
    VkImageCreateInfo image_info_val = image_info;
    VkImageViewCreateInfo view_info_val = view_info;
    
    skyboxTextureReply = resourceContext->CreateImage(
        image_info_val, 
        &view_info_val, 
        image_copies.data(), 
        image_copies.size(), 
        resource_usage::GPUOnly, 
        creationFlags, 
        (void*)skyboxTextureStr);
    
}

bool VulkanComplexScene::AllAssetsLoaded()
{
    return skyboxTextureReply->IsCompleted() && houseVboReply->IsCompleted() && houseEboReply->IsCompleted() && houseTextureReply->IsCompleted();
}

void VulkanComplexScene::WaitForAllLoaded()
{
    skyboxTextureReply->WaitForCompletion();
    houseVboReply->WaitForCompletion();
    houseEboReply->WaitForCompletion();
    houseTextureReply->WaitForCompletion();
    std::cerr << "All data loaded.";
}

const Float3 translation(0.0f, 0.0f, 0.0f);

void VulkanComplexScene::update()
{
    static auto start_time = std::chrono::high_resolution_clock::now();
    auto curr_time = std::chrono::high_resolution_clock::now();
    float diff = static_cast<float>(std::chrono::duration_cast<std::chrono::milliseconds>(curr_time - start_time).count()) / 10000.0f;
    Matrix houseMatrix = Matrix::Identity() * Matrix::Scale(Vector(scale.x, scale.y, scale.z));
    houseMatrix = houseMatrix * Matrix::RotationZ(diff * std::numbers::pi_v<float> / 2.0f) * Matrix::Translation(Vector(translation.x, translation.y, translation.z));
    houseUboData.model = FromMatrix<Float4x4>(houseMatrix);
    // persistently mapped, can just copy
#ifdef memcpy
#undef memcpy
#endif
    std::memcpy(houseUboMappedPtr, &houseUboData, sizeof(decltype(houseUboData)));
    std::memcpy(skyboxUboMappedPtr, &skyboxUboData, sizeof(decltype(skyboxUboData)));

}

void VulkanComplexScene::recordCommands()
{

    if (skyboxTextureReply && !skyboxTexture && skyboxTextureReply->IsCompleted())
    {
        skyboxTexture = skyboxTextureReply->GetResource();
        updateSkyboxDescriptorSet();
        // free shared pointers after retrieving the result
        skyboxTextureReply.reset();
    }

    if (houseTextureReply && !houseTexture && houseTextureReply->IsCompleted())
    {
        houseTexture = houseTextureReply->GetResource();
        updateHouseDescriptorSet();
        houseTextureReply.reset();
    }

    if (skyboxVboReply && !skyboxVBO && skyboxVboReply->IsCompleted())
    {
        skyboxVBO = skyboxVboReply->GetResource();
        skyboxVboReply.reset();
    }

    if (skyboxEboReply && !skyboxEBO && skyboxEboReply->IsCompleted())
    {
        skyboxEBO = skyboxEboReply->GetResource();
        skyboxEboReply.reset();
    }

    if (houseVboReply && !houseVBO && houseVboReply->IsCompleted())
    {
        houseVBO = houseVboReply->GetResource();
        houseVboReply.reset();
    }

    if (houseEboReply && !houseEBO && houseEboReply->IsCompleted())
    {
        houseEBO = houseEboReply->GetResource();
        houseEboReply.reset();
    }

    if (!firstFrame[currentFrame])
    {
        cmdPool->ResetCmdBuffer(currentFrame);
    }

    constexpr static VkCommandBufferBeginInfo begin_info
    {
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        nullptr,
        VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT,
        nullptr
    };

    constexpr static std::array<VkClearValue, 2> clearValues
    {
        VkClearValue{ VkClearColorValue{ 0.0f, 0.0f, 0.5f, 1.0f } },
        VkClearValue{ 1.0f, 0 }
    };

    const VkRect2D render_area
    {
        VkOffset2D{ 0, 0 },
        VkExtent2D{ vprObjects.swapchain->Extent() }
    };

    const VkViewport viewport
    {
        0.0f,
        0.0f,
        static_cast<float>(vprObjects.swapchain->Extent().width),
        static_cast<float>(vprObjects.swapchain->Extent().height),
        0.0f,
        1.0f
    };

    const VkRect2D scissor
    {
        render_area
    };

    VkImage currentFrameBufferImage = vprObjects.swapchain->Image(currentAcquiredImage);
    VkImageView currentFrameBufferImageView = vprObjects.swapchain->ImageView(currentAcquiredImage);
    // color attachment image index is based on what we acquire from the API call, depth stencil we just round-robin
    VkImage currentDepthStencilImage = depthStencils[currentFrame].Image;
    VkImageView currentDepthStencilImageView = depthStencils[currentFrame].View;

    std::vector<VkImageMemoryBarrier2> image_barriers;
    VkImageMemoryBarrier2 image_transition0
    {
        VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        nullptr,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        0,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        firstFrame[currentFrame] ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_QUEUE_FAMILY_IGNORED,
        VK_QUEUE_FAMILY_IGNORED,
        currentFrameBufferImage,
        VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
    };

    image_barriers.emplace_back(std::move(image_transition0));

    if (firstFrame[currentFrame])
    {
        VkImageMemoryBarrier2 depth_transition0
        {
            VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            nullptr,
            VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, // depth isn't involved in the presentation engine, so this is our src stage
            0,
            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED,
            currentDepthStencilImage,
            VkImageSubresourceRange{ VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 }
        };
        image_barriers.emplace_back(std::move(depth_transition0));
    }

    const VkDependencyInfo dependency_info0
    {
        VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        nullptr,
        0,
        0, nullptr,
        0, nullptr,
        static_cast<uint32_t>(image_barriers.size()),
        image_barriers.data()
    };

    const VkImageMemoryBarrier2 transition1[]
    {
        VkImageMemoryBarrier2
        {
            VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            nullptr,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
            0,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED,
            currentFrameBufferImage,
            VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
        }
    };

    const VkDependencyInfo dependency_info1
    {
        VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        nullptr,
        0,
        0,
        nullptr,
        0,
        nullptr,
        static_cast<uint32_t>(std::size(transition1)), 
        transition1
    };

    const VkRenderingAttachmentInfo color_attachment_info
    {
        VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        nullptr,
        currentFrameBufferImageView,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_RESOLVE_MODE_NONE,
        VK_NULL_HANDLE,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_ATTACHMENT_LOAD_OP_CLEAR,
        VK_ATTACHMENT_STORE_OP_STORE,
        clearValues[0]
    };

    const VkRenderingAttachmentInfo depth_attachment_info
    {
        VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        nullptr,
        currentDepthStencilImageView,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, // it should enter rendering in this layout
        VK_RESOLVE_MODE_NONE,
        VK_NULL_HANDLE,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_ATTACHMENT_LOAD_OP_CLEAR,
        VK_ATTACHMENT_STORE_OP_DONT_CARE, // don't care about depth attachment after rendering
        clearValues[1]
    };

    const VkRenderingInfo renderingInfo
    {
        VK_STRUCTURE_TYPE_RENDERING_INFO,
        nullptr,
        0,
        render_area,
        1,
        0,
        1,
        &color_attachment_info,
        &depth_attachment_info,
        nullptr
    };

    {
        VkResult result = VK_SUCCESS;
        VkCommandBuffer currentCmdBuffer = cmdPool->GetCmdBuffer(currentFrame);
        auto& pool = *cmdPool;
        result = vkBeginCommandBuffer(currentCmdBuffer, &begin_info); VkAssert(result);
            vkCmdBeginRendering(currentCmdBuffer, &renderingInfo);
            if (skyboxTexture)
            {
                vkCmdSetViewport(currentCmdBuffer, 0, 1, &viewport);
                vkCmdSetScissor(currentCmdBuffer, 0, 1, &scissor);
                renderSkybox(currentCmdBuffer);
            }
            if (houseTexture)
            {
                vkCmdSetViewport(currentCmdBuffer, 0, 1, &viewport);
                vkCmdSetScissor(currentCmdBuffer, 0, 1, &scissor);
                renderHouse(currentCmdBuffer);
            }
            vkCmdEndRendering(currentCmdBuffer);
        result = vkEndCommandBuffer(currentCmdBuffer);
        VkAssert(result);
    }

}

void VulkanComplexScene::renderHouse(VkCommandBuffer cmd)
{
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, housePipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout->vkHandle(), 0, 1, &houseSet->vkHandle(), 0, nullptr);
    const VkBuffer buffers[1]{ (VkBuffer)houseVBO.VkHandle };
    constexpr static VkDeviceSize offsets[1]{ 0 };
    vkCmdBindVertexBuffers(cmd, 0, 1, buffers, offsets);
    vkCmdBindIndexBuffer(cmd, (VkBuffer)houseEBO.VkHandle, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, houseIndexCount, 1, 0, 0, 0);
}

void VulkanComplexScene::renderSkybox(VkCommandBuffer cmd)
{
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, skyboxPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout->vkHandle(), 0, 1, &skyboxSet->vkHandle(), 0, nullptr);
    const VkBuffer buffer[1]{ (VkBuffer)skyboxVBO.VkHandle };
    constexpr static VkDeviceSize offsets[1]{ 0 };
    vkCmdBindVertexBuffers(cmd, 0, 1, buffer, offsets);
    vkCmdBindIndexBuffer(cmd, (VkBuffer)skyboxEBO.VkHandle, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, skyboxIndexCount, 1, 0, 0, 0);
}

void VulkanComplexScene::draw() 
{
    constexpr static VkPipelineStageFlags waitStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSemaphore imageAcquireSemaphore = imageAcquireSemaphores[currentFrame]->vkHandle();
    VkSemaphore renderCompleteSemaphore = renderCompleteSemaphores[currentFrame]->vkHandle();

    const VkSemaphoreSubmitInfo imageAcquireSemaphoreInfo
    {
        VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        nullptr,
        imageAcquireSemaphore,
        0,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        0
    };

    const VkSemaphoreSubmitInfo renderCompleteSemaphoreInfo
    {
        VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        nullptr,
        renderCompleteSemaphore,
        0,
        VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
        0
    };

    VkCommandBuffer currentCmdBuffer = cmdPool->GetCmdBuffer(currentFrame);
    const VkCommandBufferSubmitInfo cmdBufferSubmitInfo
    {
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        nullptr,
        currentCmdBuffer,
        0
    };

    const VkSubmitInfo2 submission2
    {
        VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        nullptr,
        0,
        1,
        &imageAcquireSemaphoreInfo,
        1,
        &cmdBufferSubmitInfo,
        1,
        &renderCompleteSemaphoreInfo
    };

    VkQueue graphicsQueue = vprObjects.device->GraphicsQueue();
    VkResult result = vkQueueSubmit2(graphicsQueue, 1, &submission2, endFrameFences[currentFrame]->vkHandle());
    VkAssert(result);
}

void VulkanComplexScene::endFrame()
{
    VulkanScene::endFrame();
}

void VulkanComplexScene::createSampler()
{
    constexpr static VkSamplerCreateInfo sampler_info
    {
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
        VK_COMPARE_OP_NEVER,
        0.0f,
        3.0f,
        VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
        VK_FALSE
    };
    constexpr const char* samplerStr{ "SharedSampler" };
    samplerReply = resourceContext->CreateSampler(sampler_info, (void*)samplerStr);
}

void VulkanComplexScene::createUBOs()
{
    constexpr static VkBufferCreateInfo ubo_info
    {
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        nullptr,
        0,
        sizeof(ubo_data_t),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_SHARING_MODE_EXCLUSIVE,
        0,
        nullptr
    };
    constexpr resource_creation_flags uboFlags
    { 
        resource_creation_flag_bits::UserDataAsString |
        resource_creation_flag_bits::CreateMapped |
        resource_creation_flag_bits::PersistentlyMapped
    };
    constexpr const char* houseUboStr{ "HouseUBO" };
    constexpr const char* skyboxUboStr{ "SkyboxUBO" };
    
    VkBufferCreateInfo ubo_info_val = ubo_info;
    
    // We queue the messages all in a row, and then wait in sequence to maybe make things a little less inefficient
    // Should be quick anyways because these buffers are small and persistently mapped anyways.

    auto houseUBOReply = resourceContext->CreateBuffer(
        ubo_info_val, 
        nullptr, 
        nullptr, 
        0, 
        resource_usage::CPUOnly, 
        uboFlags, 
        (void*)houseUboStr);
    
    auto skyboxUBOReply = resourceContext->CreateBuffer(
        ubo_info_val, 
        nullptr, 
        nullptr, 
        0, 
        resource_usage::CPUOnly, 
        uboFlags, 
        (void*)skyboxUboStr);

    assert(houseUBOReply->WaitForCompletion() == MessageReply::Status::Completed);
    houseUBO = houseUBOReply->GetResource();
    // can't map until we get the final result resource back to houseUBO
    auto houseUboMapReply = resourceContext->MapBuffer(houseUBO, sizeof(ubo_data_t), 0);
    houseUboMapReply->WaitForCompletion();
    houseUboMappedPtr = houseUboMapReply->GetPointer();
    assert(skyboxUBOReply->WaitForCompletion() == MessageReply::Status::Completed);
    skyboxUBO = skyboxUBOReply->GetResource();
    auto skyboxUboMapReply = resourceContext->MapBuffer(skyboxUBO, sizeof(ubo_data_t), 0);
    skyboxUboMapReply->WaitForCompletion();
    skyboxUboMappedPtr = skyboxUboMapReply->GetPointer();
}

void VulkanComplexScene::createSkyboxMesh()
{
    skybox_mesh_data_t mesh_data;

    const uint32_t queueFamilyIndices[2]
    {
        vprObjects.device->QueueFamilyIndices().Graphics,
        vprObjects.device->QueueFamilyIndices().Transfer
    };

    const VkBufferCreateInfo vbo_info
    {
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        nullptr,
        0,
        static_cast<VkDeviceSize>(sizeof(Float3) * mesh_data.vertices.size()),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_SHARING_MODE_CONCURRENT,
        2u,
        queueFamilyIndices
    };

    const VkBufferCreateInfo ebo_info
    {
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        nullptr,
        0,
        static_cast<VkDeviceSize>(sizeof(uint32_t) * mesh_data.indices.size()),
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_SHARING_MODE_CONCURRENT,
        2u,
        queueFamilyIndices
    };

    const RhiBufferResourceData vbo_data
    {
        mesh_data.vertices.data(),
        vbo_info.size,
        0,
        0
    };

    const RhiBufferResourceData ebo_data
    {
        mesh_data.indices.data(),
        ebo_info.size,
        0,
        0
    };

    constexpr resource_creation_flags creationFlags{ resource_creation_flag_bits::UserDataAsString };
    constexpr const char* skyboxVboStr{ "SkyboxVBO" };
    constexpr const char* skyboxEboStr{ "SkyboxEBO" };
    
    VkBufferCreateInfo vbo_info_val = vbo_info;
    VkBufferCreateInfo ebo_info_val = ebo_info;
    
    skyboxVboReply = resourceContext->CreateBuffer(
        vbo_info_val, 
        nullptr, 
        &vbo_data, 
        1, 
        resource_usage::GPUOnly, 
        creationFlags, 
        (void*)skyboxVboStr);
    
    skyboxEboReply = resourceContext->CreateBuffer(
        ebo_info_val, 
        nullptr, 
        &ebo_data, 
        1, 
        resource_usage::GPUOnly, 
        creationFlags, 
        (void*)skyboxEboStr);
    
    skyboxIndexCount = static_cast<uint32_t>(mesh_data.indices.size());
}

void VulkanComplexScene::createCommandPool()
{
    const VkCommandPoolCreateInfo create_info
    {
        VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        nullptr,
        VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT | VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
        vprObjects.device->QueueFamilyIndices().Graphics
    };

    cmdPool = std::make_unique<vpr::CommandPool>(vprObjects.device->vkHandle(), create_info);
    cmdPool->AllocateCmdBuffers(vprObjects.swapchain->ImageCount(), VK_COMMAND_BUFFER_LEVEL_PRIMARY);
}

void VulkanComplexScene::createDescriptorPool()
{
    descriptorPool = std::make_unique<vpr::DescriptorPool>(vprObjects.device->vkHandle(), 3, VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT);
    descriptorPool->AddResourceType(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2);
    descriptorPool->AddResourceType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2);
    descriptorPool->Create();
}

void VulkanComplexScene::createDescriptorSetLayouts()
{

    constexpr static VkDescriptorSetLayoutBinding unique_bindings[2]
    {
        VkDescriptorSetLayoutBinding
        {
            0,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            1,
            VK_SHADER_STAGE_VERTEX_BIT,
            nullptr
        },
        VkDescriptorSetLayoutBinding
        {
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

void VulkanComplexScene::createDescriptorSets()
{
    houseSet = std::make_unique<vpr::DescriptorSet>(vprObjects.device->vkHandle());
    skyboxSet = std::make_unique<vpr::DescriptorSet>(vprObjects.device->vkHandle());
    // fine here because we wait for these UBOs to create in their function already
    houseSet->AddDescriptorInfo(VkDescriptorBufferInfo{ (VkBuffer)houseUBO.VkHandle, 0, sizeof(ubo_data_t) }, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0);
    skyboxSet->AddDescriptorInfo(VkDescriptorBufferInfo{ (VkBuffer)skyboxUBO.VkHandle, 0, sizeof(ubo_data_t) }, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0);
}

void VulkanComplexScene::createUpdateTemplates()
{
    const VkDescriptorUpdateTemplateCreateInfo house_info
    {
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

void VulkanComplexScene::createPipelineLayouts()
{  
    const VkDescriptorSetLayout set_layouts[1]{ setLayout->vkHandle() };
    pipelineLayout = std::make_unique<vpr::PipelineLayout>(vprObjects.device->vkHandle());
    pipelineLayout->Create(1, set_layouts);
    RenderingContext::SetObjectName(VK_OBJECT_TYPE_PIPELINE_LAYOUT, (uint64_t)pipelineLayout->vkHandle(), "PipelineLayout");
}

void VulkanComplexScene::createShaders()
{
    houseVert = std::make_unique<vpr::ShaderModule>(vprObjects.device->vkHandle(), VK_SHADER_STAGE_VERTEX_BIT, house_shader_vert_spv, static_cast<uint32_t>(sizeof(house_shader_vert_spv)));
    RenderingContext::SetObjectName(VK_OBJECT_TYPE_SHADER_MODULE, (uint64_t)houseVert->vkHandle(), "HouseVertexShader");
    houseFrag = std::make_unique<vpr::ShaderModule>(vprObjects.device->vkHandle(), VK_SHADER_STAGE_FRAGMENT_BIT, house_shader_frag_spv, static_cast<uint32_t>(sizeof(house_shader_frag_spv)));
    RenderingContext::SetObjectName(VK_OBJECT_TYPE_SHADER_MODULE, (uint64_t)houseFrag->vkHandle(), "HouseFragmentShader");
    skyboxVert = std::make_unique<vpr::ShaderModule>(vprObjects.device->vkHandle(), VK_SHADER_STAGE_VERTEX_BIT, skybox_shader_vert_spv, static_cast<uint32_t>(sizeof(skybox_shader_vert_spv)));
    RenderingContext::SetObjectName(VK_OBJECT_TYPE_SHADER_MODULE, (uint64_t)skyboxVert->vkHandle(), "SkyboxVertexShader");
    skyboxFrag = std::make_unique<vpr::ShaderModule>(vprObjects.device->vkHandle(), VK_SHADER_STAGE_FRAGMENT_BIT, skybox_shader_frag_spv, static_cast<uint32_t>(sizeof(skybox_shader_frag_spv)));
    RenderingContext::SetObjectName(VK_OBJECT_TYPE_SHADER_MODULE, (uint64_t)skyboxFrag->vkHandle(), "SkyboxFragmentShader");
}

void VulkanComplexScene::createHousePipeline()
{

    const VkPipelineShaderStageCreateInfo shader_stages[2]
    {
        houseVert->PipelineInfo(),
        houseFrag->PipelineInfo()
    };

    constexpr static VkVertexInputBindingDescription vertex_bindings[1]
    {
        VkVertexInputBindingDescription{ 0, sizeof(LoadedObjModel::vertex_t), VK_VERTEX_INPUT_RATE_VERTEX },
    };

    constexpr static VkVertexInputAttributeDescription vertex_attrs[2]
    {
        VkVertexInputAttributeDescription{ 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 },
        VkVertexInputAttributeDescription{ 1, 0, VK_FORMAT_R32G32_SFLOAT, sizeof(Float3) }
    };

    constexpr static VkPipelineVertexInputStateCreateInfo vertex_info
    {
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        nullptr,
        0,
        1,
        vertex_bindings,
        2,
        vertex_attrs
    };

    houseCache = std::make_unique<vpr::PipelineCache>(vprObjects.device->vkHandle(), vprObjects.physicalDevice->vkHandle(), sharedCache->vkHandle(), typeid(VulkanComplexScene).hash_code() + std::hash<std::string>()("HouseCache"));

    const VkFormat color_formats[1]
    {
        vprObjects.swapchain->ColorFormat()
    };

    const VkPipelineRenderingCreateInfo rendering_info
    {
        VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        nullptr,
        0,
        std::size(color_formats),
        color_formats,
        depthStencils[0].Format, // format is same among all depth/stencil attachments
        VK_FORMAT_UNDEFINED
    };

    BasicPipelineCreateInfo housePipelineInfo
    {
        vprObjects.device,
        pipelineFlags,
        2,
        shader_stages,
        &vertex_info,
        pipelineLayout->vkHandle(),
        &rendering_info,
        VK_COMPARE_OP_LESS,
        houseCache->vkHandle(),
        VK_NULL_HANDLE,
        VK_CULL_MODE_BACK_BIT
    };

    housePipeline = CreateBasicPipeline(housePipelineInfo);

//#ifndef _NDEBUG
//    if (houseExecutableInfo != nullptr)
//    {
//        delete houseExecutableInfo;
//        houseExecutableInfo = nullptr;
//    }
//    houseExecutableInfo = new PipelineExecutableInfo(2, housePipeline);
//    RetrievePipelineExecutableInfo(vprObjects.device->vkHandle(), *houseExecutableInfo);
//#endif

}

void VulkanComplexScene::createSkyboxPipeline()
{

    const VkPipelineShaderStageCreateInfo shader_stages[2]
    {
        skyboxVert->PipelineInfo(),
        skyboxFrag->PipelineInfo()
    };

    constexpr static VkVertexInputBindingDescription vertex_bindings[1]
    {
        VkVertexInputBindingDescription{ 0, sizeof(Float3), VK_VERTEX_INPUT_RATE_VERTEX }
    };

    constexpr static VkVertexInputAttributeDescription vertex_attr[1]
    {
        VkVertexInputAttributeDescription{ 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 }
    };

    constexpr static VkPipelineVertexInputStateCreateInfo vertex_info
    {
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        nullptr,
        0,
        1,
        vertex_bindings,
        1,
        vertex_attr
    };

    skyboxCache = std::make_unique<vpr::PipelineCache>(vprObjects.device->vkHandle(), vprObjects.physicalDevice->vkHandle(), sharedCache->vkHandle(), typeid(VulkanComplexScene).hash_code() + std::hash<std::string>()("SkyboxCache"));

    const VkFormat color_formats[1]
    {
        vprObjects.swapchain->ColorFormat()
    };

    const VkPipelineRenderingCreateInfo rendering_info
    {
        VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        nullptr,
        0,
        std::size(color_formats),
        color_formats,
        depthStencils[0].Format, // format is same among all depth/stencil attachments
        VK_FORMAT_UNDEFINED
    };

    BasicPipelineCreateInfo createInfo
    {
        vprObjects.device,
        pipelineFlags,
        2,
        shader_stages,
        &vertex_info,
        pipelineLayout->vkHandle(),
        &rendering_info,
        VK_COMPARE_OP_LESS_OR_EQUAL,
        skyboxCache->vkHandle(),
        housePipeline,
        VK_CULL_MODE_NONE
    };

    skyboxPipeline = CreateBasicPipeline(createInfo);

//#ifndef _NDEBUG
//    if (skyboxExecutableInfo != nullptr)
//    {
//        delete skyboxExecutableInfo;
//        skyboxExecutableInfo = nullptr;
//    }
//    skyboxExecutableInfo = new PipelineExecutableInfo(2, skyboxPipeline);
//    RetrievePipelineExecutableInfo(vprObjects.device->vkHandle(), *skyboxExecutableInfo);
//#endif // !_NDEBUG

}

void VulkanComplexScene::updateHouseDescriptorSet()
{
    houseSet->AddDescriptorInfo(VkDescriptorImageInfo{ (VkSampler)sampler.VkHandle, (VkImageView)houseTexture.VkViewHandle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1);
    houseSet->Init(descriptorPool->vkHandle(), setLayout->vkHandle());
}

void VulkanComplexScene::updateSkyboxDescriptorSet()
{
    skyboxSet->AddDescriptorInfo(VkDescriptorImageInfo{ (VkSampler)sampler.VkHandle, (VkImageView)skyboxTexture.VkViewHandle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1);
    skyboxSet->Init(descriptorPool->vkHandle(), setLayout->vkHandle());
}
