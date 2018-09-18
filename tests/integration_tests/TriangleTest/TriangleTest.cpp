
#include "TriangleTest.hpp"
#include "PhysicalDevice.hpp"
#include "LogicalDevice.hpp"
#include "ShaderModule.hpp"
#include "Swapchain.hpp"
#include "vkAssert.hpp"
#include "Semaphore.hpp"
#include <fstream>

constexpr static const uint32_t triangle_vert_shader_spv[349] = {
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

constexpr static const uint32_t triangle_frag_shader_spv[133] = {
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

VulkanTriangle::~VulkanTriangle() {
    if (setup) {
        Destroy();
    }
}

VulkanTriangle& VulkanTriangle::GetScene() {
    static VulkanTriangle app;
    return app;
}

void VulkanTriangle::Construct(RequiredVprObjects objects, void* user_data) {
    vprObjects = objects;
    prepareVertices();
    setupUniformBuffer();
    setupCommandPool();
    setupCommandBuffers();
    setupDescriptorPool();
    setupLayouts();
    setupDescriptorSet();
    setupShaderModules();
    setupDepthStencil();
    setupRenderpass();
    setupPipeline();
    setupFramebuffers();
    setupSyncPrimitives();
    createSemaphores();
    setup = true;
    limiterA = std::chrono::system_clock::now();
    limiterB = std::chrono::system_clock::now();
}

void VulkanTriangle::Destroy() {
    vkDeviceWaitIdle(vprObjects.device->vkHandle());
    imageAcquireSemaphore.reset();
    renderCompleteSemaphore.reset();
    for (auto& fence : fences) {
        vkDestroyFence(vprObjects.device->vkHandle(), fence, nullptr);
    }
    for (auto& fbuff : framebuffers) {
        vkDestroyFramebuffer(vprObjects.device->vkHandle(), fbuff, nullptr);
    }
    vkDestroyPipeline(vprObjects.device->vkHandle(), pipeline, nullptr);
    vkDestroyRenderPass(vprObjects.device->vkHandle(), renderpass, nullptr);
    vkFreeMemory(vprObjects.device->vkHandle(), depthStencil.Memory, nullptr);
    vkDestroyImageView(vprObjects.device->vkHandle(), depthStencil.View, nullptr);
    vkDestroyImage(vprObjects.device->vkHandle(), depthStencil.Image, nullptr);
    vkDestroyShaderModule(vprObjects.device->vkHandle(), fragmentShader, nullptr);
    vkDestroyShaderModule(vprObjects.device->vkHandle(), vertexShader, nullptr);
    vkFreeDescriptorSets(vprObjects.device->vkHandle(), descriptorPool, 1, &descriptorSet);
    vkDestroyPipelineLayout(vprObjects.device->vkHandle(), pipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(vprObjects.device->vkHandle(), setLayout, nullptr);
    vkDestroyDescriptorPool(vprObjects.device->vkHandle(), descriptorPool, nullptr);
    vkFreeCommandBuffers(vprObjects.device->vkHandle(), commandPool, static_cast<uint32_t>(drawCmdBuffers.size()), drawCmdBuffers.data());
    vkFreeMemory(vprObjects.device->vkHandle(), uniformBufferVS.memory, nullptr);
    vkDestroyBuffer(vprObjects.device->vkHandle(), uniformBufferVS.buffer, nullptr);
    vkFreeMemory(vprObjects.device->vkHandle(), Indices.memory, nullptr);
    vkDestroyBuffer(vprObjects.device->vkHandle(), Indices.buffer, nullptr);
    vkFreeMemory(vprObjects.device->vkHandle(), Vertices.memory, nullptr);
    vkDestroyBuffer(vprObjects.device->vkHandle(), Vertices.buffer, nullptr);
}

void VulkanTriangle::prepareVertices()  {

    static const std::vector<Vertex> base_vertices {
        { { 0.5f, 0.5f, 0.0f }, { 1.0f, 0.0f, 0.0f } },
        { {-0.5f, 0.5f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
        { { 0.0f,-0.5f, 0.0f }, { 0.0f, 0.0f, 1.0f } }
    };
    
    static const std::vector<uint16_t> base_indices{ 0, 1, 2 };
    Indices.count = static_cast<uint32_t>(base_indices.size());

    VkMemoryAllocateInfo alloc_info{
        VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        nullptr
    };

    void* data;
    {
        const VkBufferCreateInfo buffer_info {
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
        alloc_info.memoryTypeIndex = GetMemoryTypeIndex(memreqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
            vprObjects.physicalDevice->GetMemoryProperties());
        vkAllocateMemory(vprObjects.device->vkHandle(), &alloc_info, nullptr, &Vertices.memory);
        vkMapMemory(vprObjects.device->vkHandle(), Vertices.memory, 0, alloc_info.allocationSize, 0, &data);
        memcpy(data, base_vertices.data(), sizeof(Vertex) * base_vertices.size());
        vkUnmapMemory(vprObjects.device->vkHandle(), Vertices.memory);
        vkBindBufferMemory(vprObjects.device->vkHandle(), Vertices.buffer, Vertices.memory, 0);

    }

    {
        const VkBufferCreateInfo buffer_info {
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
        alloc_info.memoryTypeIndex = GetMemoryTypeIndex(memreqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
            vprObjects.physicalDevice->GetMemoryProperties());
        vkAllocateMemory(vprObjects.device->vkHandle(), &alloc_info, nullptr, &Indices.memory);
        vkMapMemory(vprObjects.device->vkHandle(), Indices.memory, 0, alloc_info.allocationSize, 0, &data);
        memcpy(data, base_indices.data(), sizeof(uint16_t) * base_indices.size());
        vkUnmapMemory(vprObjects.device->vkHandle(), Indices.memory);
        vkBindBufferMemory(vprObjects.device->vkHandle(), Indices.buffer, Indices.memory, 0);
    }
}

void VulkanTriangle::setupUniformBuffer() {

    const VkBufferCreateInfo buffer_info {
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
    alloc_info.memoryTypeIndex = GetMemoryTypeIndex(memreqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
        vprObjects.physicalDevice->GetMemoryProperties());
    
    result = vkAllocateMemory(vprObjects.device->vkHandle(), &alloc_info, nullptr, &uniformBufferVS.memory);
    VkAssert(result);
    result = vkBindBufferMemory(vprObjects.device->vkHandle(), uniformBufferVS.buffer, uniformBufferVS.memory, 0);
    VkAssert(result);

    uniformBufferVS.descriptor = VkDescriptorBufferInfo{ uniformBufferVS.buffer, 0, sizeof(uboDataVS) };

    update();
}

void VulkanTriangle::setupCommandPool() {

    const VkCommandPoolCreateInfo pool_info {
        VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        nullptr,
        VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT | VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
        vprObjects.device->QueueFamilyIndices.Graphics
    };

    VkResult result = vkCreateCommandPool(vprObjects.device->vkHandle(), &pool_info, nullptr, &commandPool);

}

void VulkanTriangle::setupCommandBuffers() {

    drawCmdBuffers.resize(vprObjects.swapchain->ImageCount());

    const VkCommandBufferAllocateInfo cmd_info {
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        nullptr,
        commandPool,
        VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        static_cast<uint32_t>(drawCmdBuffers.size())
    };

    VkResult result = vkAllocateCommandBuffers(vprObjects.device->vkHandle(), &cmd_info, drawCmdBuffers.data());
    VkAssert(result);

}

void VulkanTriangle::setupDescriptorPool()  {
    constexpr static VkDescriptorPoolSize typeCounts[1]{ 
        VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 } 
    };

    constexpr static VkDescriptorPoolCreateInfo pool_info {
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

void VulkanTriangle::setupLayouts()  {

    constexpr static VkDescriptorSetLayoutBinding layout_binding {
        0,
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        1,
        VK_SHADER_STAGE_VERTEX_BIT,
        nullptr
    };

    constexpr static VkDescriptorSetLayoutCreateInfo layout_info {
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        nullptr,
        0,
        1,
        &layout_binding
    };

    VkResult result = vkCreateDescriptorSetLayout(vprObjects.device->vkHandle(), &layout_info, nullptr, &setLayout);
    VkAssert(result);

    const VkPipelineLayoutCreateInfo pipeline_layout_info {
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

void VulkanTriangle::setupDescriptorSet()  {
    
    const VkDescriptorSetAllocateInfo alloc_info {
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        nullptr,
        descriptorPool,
        1,
        &setLayout
    };

    VkResult result = vkAllocateDescriptorSets(vprObjects.device->vkHandle(), &alloc_info, &descriptorSet);
    VkAssert(result);

    const VkWriteDescriptorSet write_descriptor {
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

void VulkanTriangle::setupShaderModules()  {

    
    constexpr static VkShaderModuleCreateInfo vertex_info {
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

void VulkanTriangle::setupDepthStencil()  {
    depthStencil = CreateDepthStencil(vprObjects.device, vprObjects.physicalDevice, vprObjects.swapchain);
}

void VulkanTriangle::setupRenderpass()  {
    renderpass = CreateBasicRenderpass(vprObjects.device, vprObjects.swapchain, depthStencil.Format);
}

void VulkanTriangle::setupPipeline()  {

    const VkPipelineShaderStageCreateInfo shader_stages[2] {
        VkPipelineShaderStageCreateInfo{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vertexShader, "main", nullptr },
        VkPipelineShaderStageCreateInfo{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fragmentShader, "main", nullptr }
    };

    constexpr static VkVertexInputBindingDescription vertex_binding {
        0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX
    };

    constexpr static VkVertexInputAttributeDescription vertex_attributes[2] {
        VkVertexInputAttributeDescription{ 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 },
        VkVertexInputAttributeDescription{ 1, 0, VK_FORMAT_R32G32B32_SFLOAT, sizeof(float) * 3 },
    };

    constexpr static VkPipelineVertexInputStateCreateInfo vertex_info {
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        nullptr,
        0,
        1,
        &vertex_binding,
        2,
        vertex_attributes
    };

    pipeline = CreateBasicPipeline(vprObjects.device, 2, shader_stages, &vertex_info, pipelineLayout, renderpass, VK_COMPARE_OP_LESS);
    
}

void VulkanTriangle::setupFramebuffers()  {
    framebuffers.resize(vprObjects.swapchain->ImageCount());
    for (size_t i = 0; i < framebuffers.size(); ++i) {
        std::array<VkImageView, 2> attachments {
            vprObjects.swapchain->ImageView(i),
            depthStencil.View
        };

        const VkFramebufferCreateInfo create_info {
            VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            nullptr,
            0,
            renderpass,
            static_cast<uint32_t>(attachments.size()),
            attachments.data(),
            vprObjects.swapchain->Extent().width,
            vprObjects.swapchain->Extent().height,
            1
        };

        VkResult result = vkCreateFramebuffer(vprObjects.device->vkHandle(), &create_info, nullptr, &framebuffers[i]);
        VkAssert(result);

    }
}

void VulkanTriangle::setupSyncPrimitives()  {

    constexpr static VkFenceCreateInfo fence_info {
        VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        nullptr,
        0
    };

    fences.resize(drawCmdBuffers.size());
    assert(fences.size() > 0);
    for (auto& fence : fences) {
        VkResult result = vkCreateFence(vprObjects.device->vkHandle(), &fence_info, nullptr, &fence);
        VkAssert(result);
    }
}

void VulkanTriangle::recordCommands()  {

    constexpr static VkCommandBufferBeginInfo begin_info {
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        nullptr,
        VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT | VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT,
        nullptr
    };

    constexpr static std::array<VkClearValue, 2> clearValues {
        VkClearValue{ VkClearColorValue{ 0.0f, 0.0f, 0.5f, 1.0f } },
        VkClearValue{ 1.0f, 0 }
    };

    const VkRect2D render_area{ 
        VkOffset2D{ 0, 0 },
        VkExtent2D{ vprObjects.swapchain->Extent() }
    };

    const VkViewport viewport {
        0.0f,
        0.0f,
        static_cast<float>(vprObjects.swapchain->Extent().width),
        static_cast<float>(vprObjects.swapchain->Extent().height),
        0.0f,
        1.0f
    };

    const VkRect2D scissor {
        render_area
    };

    VkRenderPassBeginInfo rpBegin {
        VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        nullptr,
        renderpass,
        VK_NULL_HANDLE,
        render_area,
        static_cast<uint32_t>(clearValues.size()),
        clearValues.data()
    };

    const VkImageMemoryBarrier transition0 {
        VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        nullptr,
        VK_ACCESS_MEMORY_READ_BIT,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_QUEUE_FAMILY_IGNORED,
        VK_QUEUE_FAMILY_IGNORED,
        vprObjects.swapchain->Image(currentBuffer),
        VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
    };

    {
        rpBegin.framebuffer = framebuffers[currentBuffer];

        VkResult result = VK_SUCCESS;
        result = vkBeginCommandBuffer(drawCmdBuffers[currentBuffer], &begin_info); VkAssert(result);
        //vkCmdPipelineBarrier(drawCmdBuffers[currentBuffer], VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        //    VK_DEPENDENCY_BY_REGION_BIT, 0, nullptr, 0, nullptr, 1, &transition0);
        vkCmdBeginRenderPass(drawCmdBuffers[currentBuffer], &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(drawCmdBuffers[currentBuffer], VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vkCmdBindDescriptorSets(drawCmdBuffers[currentBuffer], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
        constexpr static VkDeviceSize offsets[1]{ 0 };
        vkCmdBindVertexBuffers(drawCmdBuffers[currentBuffer], 0, 1, &Vertices.buffer, offsets);
        vkCmdBindIndexBuffer(drawCmdBuffers[currentBuffer], Indices.buffer, 0, VK_INDEX_TYPE_UINT16);
        vkCmdSetViewport(drawCmdBuffers[currentBuffer], 0, 1, &viewport);
        vkCmdSetScissor(drawCmdBuffers[currentBuffer], 0, 1, &scissor);
        vkCmdDrawIndexed(drawCmdBuffers[currentBuffer], Indices.count, 1, 0, 0, 0);
        vkCmdEndRenderPass(drawCmdBuffers[currentBuffer]);
        result = vkEndCommandBuffer(drawCmdBuffers[currentBuffer]); VkAssert(result);
    }

}

void VulkanTriangle::draw() {

    constexpr static VkPipelineStageFlags waitStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    const VkSubmitInfo submission {
        VK_STRUCTURE_TYPE_SUBMIT_INFO,
        nullptr,
        1,
        &imageAcquireSemaphore->vkHandle(),
        &waitStageMask,
        1,
        &drawCmdBuffers[currentBuffer],
        1,
        &renderCompleteSemaphore->vkHandle()
    };

    VkResult result = vkQueueSubmit(vprObjects.device->GraphicsQueue(), 1, &submission, fences[currentBuffer]);

}

void VulkanTriangle::endFrame() {
    VkResult result = vkWaitForFences(vprObjects.device->vkHandle(), 1, &fences[currentBuffer], VK_TRUE, UINT64_MAX); 
    VkAssert(result);
    result = vkResetFences(vprObjects.device->vkHandle(), 1, &fences[currentBuffer]); 
    VkAssert(result);
}

void VulkanTriangle::update() {
    uboDataVS.projection = glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 300.0f);
    uboDataVS.projection[1][1] *= -1.0f;
    uboDataVS.view = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -2.5f));
    uboDataVS.model = glm::mat4(1.0f);

    void* p_data;
    vkMapMemory(vprObjects.device->vkHandle(), uniformBufferVS.memory, 0, sizeof(uboDataVS), 0, &p_data);
    memcpy(p_data, &uboDataVS, sizeof(uboDataVS));
    vkUnmapMemory(vprObjects.device->vkHandle(), uniformBufferVS.memory);
}
