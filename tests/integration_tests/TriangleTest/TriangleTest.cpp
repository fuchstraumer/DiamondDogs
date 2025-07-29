#include "TriangleTest.hpp"
#include "RenderingContext.hpp"
#include "PhysicalDevice.hpp"
#include "LogicalDevice.hpp"
#include "ShaderModule.hpp"
#include "Swapchain.hpp"
#include "vkAssert.hpp"
#include "Semaphore.hpp"
#include "Fence.hpp"
#include "Math.hpp"
#include <fstream>
#include <numbers>
#include <format>

using namespace math;

constexpr static const uint32_t triangle_vert_shader_spv[349] =
{
	0x07230203,0x00010000,0x00080007,0x0000002c,0x00000000,0x00020011,0x00000001,0x0006000b,
	0x00000001,0x4c534c47,0x6474732e,0x3035342e,0x00000000,0x0003000e,0x00000000,0x00000001,
	0x0009000f,0x00000000,0x00000004,0x6e69616d,0x00000000,0x0000000a,0x0000001e,0x00000029,
	0x0000002a,0x00030003,0x00000002,0x000001c2,0x00090004,0x415f4c47,0x735f4252,0x72617065,
	0x5f657461,0x64616873,0x6f5f7265,0x63656a62,0x00007374,0x00040005,0x00000004,0x6e69616d,
	0x00000000,0x00060005,0x00000008,0x505f6c67,0x65567265,0x78657472,0x00000000,0x00060006,
	0x00000008,0x00000000,0x505f6c67,0x7469736f,0x006e6f69,0x00030005,0x0000000a,0x00000000,
	0x00040005,0x0000000e,0x62755f5f,0x005f5f6f,0x00050006,0x0000000e,0x00000000,0x65646f6d,
	0x0000006c,0x00050006,0x0000000e,0x00000001,0x77656976,0x00000000,0x00060006,0x0000000e,
	0x00000002,0x6a6f7270,0x69746365,0x00006e6f,0x00030005,0x00000010,0x006f6275,0x00050005,
	0x0000001e,0x69736f70,0x6e6f6974,0x00000000,0x00040005,0x00000029,0x6c6f4376,0x0000726f,
	0x00040005,0x0000002a,0x6f6c6f63,0x00000072,0x00050048,0x00000008,0x00000000,0x0000000b,
	0x00000000,0x00030047,0x00000008,0x00000002,0x00040048,0x0000000e,0x00000000,0x00000005,
	0x00050048,0x0000000e,0x00000000,0x00000023,0x00000000,0x00050048,0x0000000e,0x00000000,
	0x00000007,0x00000010,0x00040048,0x0000000e,0x00000001,0x00000005,0x00050048,0x0000000e,
	0x00000001,0x00000023,0x00000040,0x00050048,0x0000000e,0x00000001,0x00000007,0x00000010,
	0x00040048,0x0000000e,0x00000002,0x00000005,0x00050048,0x0000000e,0x00000002,0x00000023,
	0x00000080,0x00050048,0x0000000e,0x00000002,0x00000007,0x00000010,0x00030047,0x0000000e,
	0x00000002,0x00040047,0x00000010,0x00000022,0x00000000,0x00040047,0x00000010,0x00000021,
	0x00000000,0x00040047,0x0000001e,0x0000001e,0x00000000,0x00040047,0x00000029,0x0000001e,
	0x00000000,0x00040047,0x0000002a,0x0000001e,0x00000001,0x00020013,0x00000002,0x00030021,
	0x00000003,0x00000002,0x00030016,0x00000006,0x00000020,0x00040017,0x00000007,0x00000006,
	0x00000004,0x0003001e,0x00000008,0x00000007,0x00040020,0x00000009,0x00000003,0x00000008,
	0x0004003b,0x00000009,0x0000000a,0x00000003,0x00040015,0x0000000b,0x00000020,0x00000001,
	0x0004002b,0x0000000b,0x0000000c,0x00000000,0x00040018,0x0000000d,0x00000007,0x00000004,
	0x0005001e,0x0000000e,0x0000000d,0x0000000d,0x0000000d,0x00040020,0x0000000f,0x00000002,
	0x0000000e,0x0004003b,0x0000000f,0x00000010,0x00000002,0x0004002b,0x0000000b,0x00000011,
	0x00000002,0x00040020,0x00000012,0x00000002,0x0000000d,0x0004002b,0x0000000b,0x00000015,
	0x00000001,0x00040017,0x0000001c,0x00000006,0x00000003,0x00040020,0x0000001d,0x00000001,
	0x0000001c,0x0004003b,0x0000001d,0x0000001e,0x00000001,0x0004002b,0x00000006,0x00000020,
	0x3f800000,0x00040020,0x00000026,0x00000003,0x00000007,0x00040020,0x00000028,0x00000003,
	0x0000001c,0x0004003b,0x00000028,0x00000029,0x00000003,0x0004003b,0x0000001d,0x0000002a,
	0x00000001,0x00050036,0x00000002,0x00000004,0x00000000,0x00000003,0x000200f8,0x00000005,
	0x00050041,0x00000012,0x00000013,0x00000010,0x00000011,0x0004003d,0x0000000d,0x00000014,
	0x00000013,0x00050041,0x00000012,0x00000016,0x00000010,0x00000015,0x0004003d,0x0000000d,
	0x00000017,0x00000016,0x00050092,0x0000000d,0x00000018,0x00000014,0x00000017,0x00050041,
	0x00000012,0x00000019,0x00000010,0x0000000c,0x0004003d,0x0000000d,0x0000001a,0x00000019,
	0x00050092,0x0000000d,0x0000001b,0x00000018,0x0000001a,0x0004003d,0x0000001c,0x0000001f,
	0x0000001e,0x00050051,0x00000006,0x00000021,0x0000001f,0x00000000,0x00050051,0x00000006,
	0x00000022,0x0000001f,0x00000001,0x00050051,0x00000006,0x00000023,0x0000001f,0x00000002,
	0x00070050,0x00000007,0x00000024,0x00000021,0x00000022,0x00000023,0x00000020,0x00050091,
	0x00000007,0x00000025,0x0000001b,0x00000024,0x00050041,0x00000026,0x00000027,0x0000000a,
	0x0000000c,0x0003003e,0x00000027,0x00000025,0x0004003d,0x0000001c,0x0000002b,0x0000002a,
	0x0003003e,0x00000029,0x0000002b,0x000100fd,0x00010038
};

constexpr static const uint32_t triangle_frag_shader_spv[133] =
{
	0x07230203,0x00010000,0x00080007,0x00000013,0x00000000,0x00020011,0x00000001,0x0006000b,
	0x00000001,0x4c534c47,0x6474732e,0x3035342e,0x00000000,0x0003000e,0x00000000,0x00000001,
	0x0007000f,0x00000004,0x00000004,0x6e69616d,0x00000000,0x00000009,0x0000000c,0x00030010,
	0x00000004,0x00000007,0x00030003,0x00000002,0x000001c2,0x00090004,0x415f4c47,0x735f4252,
	0x72617065,0x5f657461,0x64616873,0x6f5f7265,0x63656a62,0x00007374,0x00040005,0x00000004,
	0x6e69616d,0x00000000,0x00050005,0x00000009,0x6b636162,0x66667562,0x00007265,0x00040005,
	0x0000000c,0x6c6f4376,0x0000726f,0x00040047,0x00000009,0x0000001e,0x00000000,0x00040047,
	0x0000000c,0x0000001e,0x00000000,0x00020013,0x00000002,0x00030021,0x00000003,0x00000002,
	0x00030016,0x00000006,0x00000020,0x00040017,0x00000007,0x00000006,0x00000004,0x00040020,
	0x00000008,0x00000003,0x00000007,0x0004003b,0x00000008,0x00000009,0x00000003,0x00040017,
	0x0000000a,0x00000006,0x00000003,0x00040020,0x0000000b,0x00000001,0x0000000a,0x0004003b,
	0x0000000b,0x0000000c,0x00000001,0x0004002b,0x00000006,0x0000000e,0x3f800000,0x00050036,
	0x00000002,0x00000004,0x00000000,0x00000003,0x000200f8,0x00000005,0x0004003d,0x0000000a,
	0x0000000d,0x0000000c,0x00050051,0x00000006,0x0000000f,0x0000000d,0x00000000,0x00050051,
	0x00000006,0x00000010,0x0000000d,0x00000001,0x00050051,0x00000006,0x00000011,0x0000000d,
	0x00000002,0x00070050,0x00000007,0x00000012,0x0000000f,0x00000010,0x00000011,0x0000000e,
	0x0003003e,0x00000009,0x00000012,0x000100fd,0x00010038
};

VulkanTriangle::VulkanTriangle() : VulkanScene() {}

VulkanTriangle::~VulkanTriangle()
{
    if (setup) {
        Destroy();
    }
}

VulkanTriangle& VulkanTriangle::GetScene()
{
    static VulkanTriangle app;
    return app;
}

void VulkanTriangle::Construct(RequiredVprObjects objects, void* user_data)
{
    vprObjects = objects;
    numFramebuffers = vprObjects.swapchain->ImageCount();
    prepareVertices();
    setupUniformBuffer();
    setupCommandPool();
    setupCommandBuffers();
    setupDescriptorPool();
    setupLayouts();
    setupDescriptorSet();
    setupShaderModules();
    setupDepthStencil();
    setupPipeline();
    setupSwapchainDebugInfo();
    setupSyncPrimitives();
    createSemaphores();
    setup = true;
    limiterA = std::chrono::system_clock::now();
    limiterB = std::chrono::system_clock::now();
}

void VulkanTriangle::Destroy()
{
    vkDeviceWaitIdle(vprObjects.device->vkHandle());

    for (auto& imageAcquireSemaphore : imageAcquireSemaphores)
    {
        imageAcquireSemaphore.reset();
    }
    imageAcquireSemaphores.clear();
    imageAcquireSemaphores.shrink_to_fit();

    for (auto& renderCompleteSemaphore : renderCompleteSemaphores)
    {
        renderCompleteSemaphore.reset();
    }
    renderCompleteSemaphores.clear();
    renderCompleteSemaphores.shrink_to_fit();

    endFrameFences.clear();

    firstFrame.clear();

    vkDestroyPipeline(vprObjects.device->vkHandle(), pipeline, nullptr);

    for (auto& depthStencil : depthStencils)
    {
        vkFreeMemory(vprObjects.device->vkHandle(), depthStencil.Memory, nullptr);
        vkDestroyImageView(vprObjects.device->vkHandle(), depthStencil.View, nullptr);
        vkDestroyImage(vprObjects.device->vkHandle(), depthStencil.Image, nullptr);
    }
    depthStencils.clear();

    vkDestroyShaderModule(vprObjects.device->vkHandle(), fragmentShader, nullptr);
    vkDestroyShaderModule(vprObjects.device->vkHandle(), vertexShader, nullptr);
    vkFreeDescriptorSets(vprObjects.device->vkHandle(), descriptorPool, 1, &descriptorSet);
    vkDestroyPipelineLayout(vprObjects.device->vkHandle(), pipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(vprObjects.device->vkHandle(), setLayout, nullptr);
    vkDestroyDescriptorPool(vprObjects.device->vkHandle(), descriptorPool, nullptr);
    vkFreeCommandBuffers(vprObjects.device->vkHandle(), commandPool, static_cast<uint32_t>(drawCmdBuffers.size()), drawCmdBuffers.data());
    vkDestroyCommandPool(vprObjects.device->vkHandle(), commandPool, nullptr);
    vkFreeMemory(vprObjects.device->vkHandle(), uniformBufferVS.memory, nullptr);
    vkDestroyBuffer(vprObjects.device->vkHandle(), uniformBufferVS.buffer, nullptr);
    vkFreeMemory(vprObjects.device->vkHandle(), Indices.memory, nullptr);
    vkDestroyBuffer(vprObjects.device->vkHandle(), Indices.buffer, nullptr);
    vkFreeMemory(vprObjects.device->vkHandle(), Vertices.memory, nullptr);
    vkDestroyBuffer(vprObjects.device->vkHandle(), Vertices.buffer, nullptr);
}

void VulkanTriangle::prepareVertices()  {

    static const std::vector<Vertex> base_vertices
    {
        { { 0.5f, 0.5f, 0.0f }, { 1.0f, 0.0f, 0.0f } },
        { {-0.5f, 0.5f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
        { { 0.0f,-0.5f, 0.0f }, { 0.0f, 0.0f, 1.0f } }
    };
    
    static const std::vector<uint16_t> base_indices{ 0, 1, 2 };
    Indices.count = static_cast<uint32_t>(base_indices.size());

    VkMemoryAllocateInfo alloc_info
    {
        VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        nullptr
    };

    void* data = nullptr;

    {
        const VkBufferCreateInfo buffer_info
        {
            VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            nullptr,
            0,
            static_cast<uint32_t>(base_vertices.size() * sizeof(Vertex)),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_SHARING_MODE_EXCLUSIVE,
            0,
            nullptr
        };

        vkCreateBuffer(vprObjects.device->vkHandle(), &buffer_info, nullptr, &Vertices.buffer);
        VkMemoryRequirements memreqs{};
        vkGetBufferMemoryRequirements(vprObjects.device->vkHandle(), Vertices.buffer, &memreqs);
        alloc_info.allocationSize = memreqs.size;
        alloc_info.memoryTypeIndex = vprObjects.device->GetMemoryTypeIdx(memreqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
        vkAllocateMemory(vprObjects.device->vkHandle(), &alloc_info, nullptr, &Vertices.memory);
        vkMapMemory(vprObjects.device->vkHandle(), Vertices.memory, 0, alloc_info.allocationSize, 0, &data);
        memcpy(data, base_vertices.data(), sizeof(Vertex) * base_vertices.size());
        vkUnmapMemory(vprObjects.device->vkHandle(), Vertices.memory);
        vkBindBufferMemory(vprObjects.device->vkHandle(), Vertices.buffer, Vertices.memory, 0);

    }

    {
        const VkBufferCreateInfo buffer_info
        {
            VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            nullptr,
            0,
            static_cast<uint32_t>(base_indices.size() * sizeof(uint16_t)),
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            VK_SHARING_MODE_EXCLUSIVE,
            0,
            nullptr
        };

        vkCreateBuffer(vprObjects.device->vkHandle(), &buffer_info, nullptr, &Indices.buffer);
        VkMemoryRequirements memreqs{};
        vkGetBufferMemoryRequirements(vprObjects.device->vkHandle(), Indices.buffer, &memreqs);
        alloc_info.allocationSize = memreqs.size;
        alloc_info.memoryTypeIndex = vprObjects.device->GetMemoryTypeIdx(memreqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
        vkAllocateMemory(vprObjects.device->vkHandle(), &alloc_info, nullptr, &Indices.memory);
        vkMapMemory(vprObjects.device->vkHandle(), Indices.memory, 0, alloc_info.allocationSize, 0, &data);
        memcpy(data, base_indices.data(), sizeof(uint16_t) * base_indices.size());
        vkUnmapMemory(vprObjects.device->vkHandle(), Indices.memory);
        vkBindBufferMemory(vprObjects.device->vkHandle(), Indices.buffer, Indices.memory, 0);
    }
}

void VulkanTriangle::setupUniformBuffer()
{

    const VkBufferCreateInfo buffer_info
    {
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        nullptr,
        0,
        sizeof(uboDataVS),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_SHARING_MODE_EXCLUSIVE,
        0,
        nullptr
    };

    VkResult result = vkCreateBuffer(vprObjects.device->vkHandle(), &buffer_info, nullptr, &uniformBufferVS.buffer);
    VkAssert(result);

    VkMemoryRequirements memreqs;
    vkGetBufferMemoryRequirements(vprObjects.device->vkHandle(), uniformBufferVS.buffer, &memreqs);
    VkMemoryAllocateInfo alloc_info{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr };
    alloc_info.allocationSize = memreqs.size;
    alloc_info.memoryTypeIndex = vprObjects.device->GetMemoryTypeIdx(memreqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    
    result = vkAllocateMemory(vprObjects.device->vkHandle(), &alloc_info, nullptr, &uniformBufferVS.memory);
    VkAssert(result);
    result = vkBindBufferMemory(vprObjects.device->vkHandle(), uniformBufferVS.buffer, uniformBufferVS.memory, 0);
    VkAssert(result);

    uniformBufferVS.descriptor = VkDescriptorBufferInfo{ uniformBufferVS.buffer, 0, sizeof(uboDataVS) };

    update();
}

void VulkanTriangle::setupCommandPool()
{

    const VkCommandPoolCreateInfo pool_info
    {
        VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        nullptr,
        VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        vprObjects.device->QueueFamilyIndices().Graphics
    };

    VkResult result = vkCreateCommandPool(vprObjects.device->vkHandle(), &pool_info, nullptr, &commandPool);

}

void VulkanTriangle::setupCommandBuffers()
{

    drawCmdBuffers.resize(vprObjects.swapchain->ImageCount());

    const VkCommandBufferAllocateInfo cmd_info
    {
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        nullptr,
        commandPool,
        VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        static_cast<uint32_t>(drawCmdBuffers.size())
    };

    VkResult result = vkAllocateCommandBuffers(vprObjects.device->vkHandle(), &cmd_info, drawCmdBuffers.data());
    VkAssert(result);

    uint32_t i = 0;
    for (VkCommandBuffer cmdBuffer : drawCmdBuffers)
    {
        std::string cmdBufferName = std::format("DrawCmdBuffer_{}", i);
        const uint64_t handleCast = reinterpret_cast<uint64_t>(cmdBuffer);
        RenderingContext::SetObjectName(VK_OBJECT_TYPE_COMMAND_BUFFER, handleCast, cmdBufferName.c_str());
        ++i;
    }

}

void VulkanTriangle::setupDescriptorPool()
{
    
    constexpr static VkDescriptorPoolSize typeCounts[1]
    { 
        VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 } 
    };

    constexpr static VkDescriptorPoolCreateInfo pool_info
    {
        VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        nullptr,
        VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
        1,
        1,
        typeCounts
    };

    VkResult result = vkCreateDescriptorPool(vprObjects.device->vkHandle(), &pool_info, nullptr, &descriptorPool);
    VkAssert(result);
}

void VulkanTriangle::setupLayouts()
{

    constexpr static VkDescriptorSetLayoutBinding layout_binding
    {
        0,
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        1,
        VK_SHADER_STAGE_VERTEX_BIT,
        nullptr
    };

    constexpr static VkDescriptorSetLayoutCreateInfo layout_info
    {
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        nullptr,
        0,
        1,
        &layout_binding
    };

    VkResult result = vkCreateDescriptorSetLayout(vprObjects.device->vkHandle(), &layout_info, nullptr, &setLayout);
    VkAssert(result);

    const VkPipelineLayoutCreateInfo pipeline_layout_info
    {
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        nullptr,
        0,
        1,
        &setLayout,
        0,
        nullptr
    };

    result = vkCreatePipelineLayout(vprObjects.device->vkHandle(), &pipeline_layout_info, nullptr, &pipelineLayout);
    VkAssert(result);

}

void VulkanTriangle::setupDescriptorSet()
{
    
    const VkDescriptorSetAllocateInfo alloc_info
    {
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        nullptr,
        descriptorPool,
        1,
        &setLayout
    };

    VkResult result = vkAllocateDescriptorSets(vprObjects.device->vkHandle(), &alloc_info, &descriptorSet);
    VkAssert(result);

    const VkWriteDescriptorSet write_descriptor
    {
        VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        nullptr,
        descriptorSet,
        0,
        0,
        1,
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        nullptr,
        &uniformBufferVS.descriptor,
        nullptr
    };

    vkUpdateDescriptorSets(vprObjects.device->vkHandle(), 1, &write_descriptor, 0, nullptr);
}

void VulkanTriangle::setupShaderModules() 
{

    
    constexpr static VkShaderModuleCreateInfo vertex_info
    {
        VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        nullptr,
        0,
        sizeof(triangle_vert_shader_spv),
        triangle_vert_shader_spv
    };
    
    vertexShader = VK_NULL_HANDLE;
    VkResult result = vkCreateShaderModule(vprObjects.device->vkHandle(), &vertex_info, nullptr, &vertexShader);
    VkAssert(result);

    constexpr static VkShaderModuleCreateInfo fragment_info{
        VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        nullptr,
        0,
        sizeof(triangle_frag_shader_spv),
        triangle_frag_shader_spv
    };

    fragmentShader = VK_NULL_HANDLE;
    result = vkCreateShaderModule(vprObjects.device->vkHandle(), &fragment_info, nullptr, &fragmentShader);
    VkAssert(result);

}

void VulkanTriangle::setupDepthStencil() 
{
    for (uint32_t i = 0; i < numFramebuffers; ++i)
    {
        depthStencils.emplace_back(CreateDepthStencil(vprObjects.device, vprObjects.physicalDevice, vprObjects.swapchain));
    }
}

void VulkanTriangle::setupPipeline() 
{

    const VkPipelineShaderStageCreateInfo shader_stages[2]
    {
        VkPipelineShaderStageCreateInfo{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vertexShader, "main", nullptr },
        VkPipelineShaderStageCreateInfo{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fragmentShader, "main", nullptr }
    };

    constexpr static VkVertexInputBindingDescription vertex_binding
    {
        0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX
    };

    constexpr static VkVertexInputAttributeDescription vertex_attributes[2]
    {
        VkVertexInputAttributeDescription{ 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 },
        VkVertexInputAttributeDescription{ 1, 0, VK_FORMAT_R32G32B32_SFLOAT, sizeof(float) * 3 },
    };

    constexpr static VkPipelineVertexInputStateCreateInfo vertex_info
    {
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        nullptr,
        0,
        1,
        &vertex_binding,
        2,
        vertex_attributes
    };

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

    BasicPipelineCreateInfo pipelineCreateInfo
    {
        vprObjects.device,
        0,
        2,
        shader_stages,
        &vertex_info,
        pipelineLayout,
        &rendering_info,
        VK_COMPARE_OP_LESS
    };

    pipeline = CreateBasicPipeline(pipelineCreateInfo);
    
}

void VulkanTriangle::setupSwapchainDebugInfo()
{
    const uint64_t swapchainHandle = reinterpret_cast<uint64_t>(vprObjects.swapchain->vkHandle());
    RenderingContext::SetObjectName(VK_OBJECT_TYPE_SWAPCHAIN_KHR, swapchainHandle, "Swapchain");

    const uint32_t swapchainImageCount = vprObjects.swapchain->ImageCount();
    for (uint32_t i = 0; i < swapchainImageCount; ++i)
    {
        const uint64_t image = reinterpret_cast<uint64_t>(vprObjects.swapchain->Image(i));
        std::string imageName = std::format("SwapchainImage_{}", i);
        RenderingContext::SetObjectName(VK_OBJECT_TYPE_IMAGE, image, imageName.c_str());
        const uint64_t imageView = reinterpret_cast<uint64_t>(vprObjects.swapchain->ImageView(i));
        std::string imageViewName = std::format("SwapchainImageView_{}", i);
        RenderingContext::SetObjectName(VK_OBJECT_TYPE_IMAGE_VIEW, imageView, imageViewName.c_str());
    }
}

void VulkanTriangle::setupSyncPrimitives()
{

    constexpr static VkFenceCreateInfo fence_info
    {
        VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        nullptr,
        0
    };

    assert(fences.size() > 0);
    for (auto& fence : fences)
    {
        VkResult result = vkCreateFence(vprObjects.device->vkHandle(), &fence_info, nullptr, &fence);
        VkAssert(result);
    }

    for (uint32_t i = 0; i < numFramebuffers; ++i)
    {
        endFrameFences.emplace_back(std::make_unique<vpr::Fence>(vprObjects.device->vkHandle(), VK_FENCE_CREATE_SIGNALED_BIT));
        VkFence createdFence = endFrameFences.back()->vkHandle();
        std::string fenceName = std::format("EndFrameFence_{}", i);
        RenderingContext::SetObjectName(VK_OBJECT_TYPE_FENCE, (uint64_t)createdFence, fenceName.c_str());
    }
}

void VulkanTriangle::recordCommands()
{

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
        
        VkCommandBuffer currentBuffer = drawCmdBuffers[currentFrame];
        VkResult result = VK_SUCCESS;
        result = vkBeginCommandBuffer(drawCmdBuffers[currentFrame], &begin_info);
        VkAssert(result);
            vkCmdPipelineBarrier2(currentBuffer, &dependency_info0);
            vkCmdBeginRendering(currentBuffer, &renderingInfo);
            vkCmdBindPipeline(currentBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            vkCmdBindDescriptorSets(currentBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
            constexpr static VkDeviceSize offsets[1]{ 0 };
            vkCmdBindVertexBuffers(currentBuffer, 0, 1, &Vertices.buffer, offsets);
            vkCmdBindIndexBuffer(currentBuffer, Indices.buffer, 0, VK_INDEX_TYPE_UINT16);
            vkCmdSetViewport(currentBuffer, 0, 1, &viewport);
            vkCmdSetScissor(currentBuffer, 0, 1, &scissor);
            vkCmdDrawIndexed(currentBuffer, Indices.count, 1, 0, 0, 0);
            vkCmdEndRendering(currentBuffer);
            vkCmdPipelineBarrier2(currentBuffer, &dependency_info1);
        result = vkEndCommandBuffer(currentBuffer);
        VkAssert(result);
    }

}

void VulkanTriangle::draw()
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

    const VkCommandBufferSubmitInfo cmdBufferSubmitInfo
    {
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        nullptr,
        drawCmdBuffers[currentFrame],
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

void VulkanTriangle::endFrame()
{
    VulkanScene::endFrame();
}

void VulkanTriangle::update()
{
    constexpr float radians_ratio = std::numbers::pi_v<float> / 180.0f;
    const float window_width = static_cast<float>(vprObjects.swapchain->Extent().width);
    const float window_height = static_cast<float>(vprObjects.swapchain->Extent().height);
    math::Matrix projection_matrix = math::Matrix::PerspectiveRH(60.0f * radians_ratio, window_width / window_height, 0.1f, 300.0f);
    //projection_matrix = projection_matrix.Transpose(); // Vulkan expects column-major matrices
    // need to set [1][1] *= -1.0f to flip the Y axis
    uboDataVS.projection = math::FromMatrix<Float4x4>(projection_matrix);
    uboDataVS.projection(1, 1) *= -1.0f;
    math::Matrix view_matrix = math::Matrix::Identity();
    view_matrix = view_matrix.Translation(0.0f, 0.0f, -2.5f);
    //view_matrix = view_matrix.Transpose(); // Vulkan expects column-major matrices
    uboDataVS.view = FromMatrix<Float4x4>(view_matrix);
    uboDataVS.model = Float4x4::Identity();

    void* p_data;
    vkMapMemory(vprObjects.device->vkHandle(), uniformBufferVS.memory, 0, sizeof(uboDataVS), 0, &p_data);
    memcpy(p_data, &uboDataVS, sizeof(uboDataVS));
    vkUnmapMemory(vprObjects.device->vkHandle(), uniformBufferVS.memory);
}
