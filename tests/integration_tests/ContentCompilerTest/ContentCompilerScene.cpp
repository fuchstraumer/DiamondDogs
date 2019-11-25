#include "ContentCompilerScene.hpp"
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
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
#include "ObjModel.hpp"
#include "generation/Compiler.hpp"
#include <iterator>

constexpr const char* vertexShaderSrc = R"(
#version 450
#extension GL_ARB_separate_shader_objects : enable

layout (location = 0) in vec3 pos;
layout (location = 1) in vec2 uv;

layout (set = 0, binding = 0) uniform uniform_buffer {
    mat4 model;
    mat4 view;
    mat4 projection;
} ubo;

out gl_PerVertex {
    vec4 gl_Position;
};

void main() {
    gl_Position = ubo.projection * ubo.view * ubo.model * vec4(pos, 1.0f);
}
)";

constexpr const char* fragmentShaderSrc = R"(
#version 450
#extension GL_ARB_separate_shader_objects : enable

layout (location = 0) out vec4 backbuffer;

const float grey = 123.0f / 255.0f;

void main() {
    backbuffer = float4(grey, grey, grey, 1.0f);
}
)";

struct ubo_data_t
{
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 projection;
};

static ubo_data_t uboData;

ContentCompilerScene::ContentCompilerScene() : VulkanScene()
{
}

ContentCompilerScene::~ContentCompilerScene()
{
    Destroy();
}

void ContentCompilerScene::Construct(RequiredVprObjects objects, void* user_data)
{
    vprObjects = objects;
    modelData = reinterpret_cast<ObjectModelData*>(user_data);
    uboData.view = glm::lookAt(glm::vec3(-2.0f, -2.0f, 1.0f), glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    uboData.projection = glm::perspectiveFov(
        glm::radians(90.0f),
        static_cast<float>(vprObjects.swapchain->Extent().width),
        static_cast<float>(vprObjects.swapchain->Extent().height),
        0.01f, 5000.0f);
    uboData.model = glm::mat4(1.0f);
    pipelineCache = std::make_unique<vpr::PipelineCache>(vprObjects.device->vkHandle(), vprObjects.physicalDevice->vkHandle(), typeid(ContentCompilerScene).hash_code());
    createSemaphores();
    createUBO();
    update();
    createFences();
    createCommandPool();
    createDescriptorPool();
    createShaders();
    createSetLayouts();
    createDescriptorSet();
    createPipelineLayout();
    depthStencil = CreateDepthStencil(vprObjects.device, vprObjects.physicalDevice, vprObjects.swapchain);
    createRenderpass();
    createFramebuffers();
    createPipeline();
}

void ContentCompilerScene::Destroy()
{
    auto& resourceContext = ResourceContext::Get();
    resourceContext.DestroyResource(meshUBO);
    resourceContext.DestroyResource(meshEBO);
    resourceContext.DestroyResource(meshVBO);
    descriptorSet.reset();
    pipelineLayout.reset();
    descriptorSetLayout.reset();
    vertexShader.reset();
    fragmentShader.reset();
    imageAcquireSemaphore.reset();
    renderCompleteSemaphore.reset();
    destroyFences();
    destroyFramebuffers();
    vkDestroyRenderPass(vprObjects.device->vkHandle(), renderPass, nullptr);
    vkFreeMemory(vprObjects.device->vkHandle(), depthStencil.Memory, nullptr);
    vkDestroyImageView(vprObjects.device->vkHandle(), depthStencil.View, nullptr);
    vkDestroyImage(vprObjects.device->vkHandle(), depthStencil.Image, nullptr);
}

void ContentCompilerScene::update()
{
    static const glm::vec3 scale(1.0f);
    static const glm::vec3 translation(0.0f);

    static auto start_time = std::chrono::high_resolution_clock::now();
    auto curr_time = std::chrono::high_resolution_clock::now();
    float diff = static_cast<float>(std::chrono::duration_cast<std::chrono::milliseconds>(curr_time - start_time).count()) / 10000.0f;
    uboData.model = glm::scale(glm::mat4(1.0f), scale);
    uboData.model = glm::rotate(uboData.model, diff * glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f)); // pivot house around center axis based on time.
    uboData.model = glm::translate(uboData.model, translation);
    const gpu_resource_data_t house_ubo_data
    {
        &uboData,
        sizeof(ubo_data_t),
        0,
        VK_QUEUE_FAMILY_IGNORED
    };
    auto& resourceContext = ResourceContext::Get();
    resourceContext.SetBufferData(meshUBO, 1, &house_ubo_data);
}

void ContentCompilerScene::recordCommands()
{
    static std::array<bool, 5> first_frame{ true, true, true, true, true };

    if (!first_frame[currentBuffer])
    {
        cmdPool->ResetCmdBuffer(currentBuffer);
    }

    constexpr static VkCommandBufferBeginInfo begin_info
    {
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        nullptr,
        VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT | VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT,
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

    VkRenderPassBeginInfo rpBegin
    {
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
        vkCmdSetViewport(pool[currentBuffer], 0, 1, &viewport);
        vkCmdSetScissor(pool[currentBuffer], 0, 1, &scissor);
        renderObject(pool[currentBuffer]);
        vkCmdEndRenderPass(pool[currentBuffer]);
        result = vkEndCommandBuffer(pool[currentBuffer]);
        VkAssert(result);
    }

    first_frame[currentBuffer] = false;
}

void ContentCompilerScene::renderObject(VkCommandBuffer cmd)
{
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, modelPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout->vkHandle(), 0u, 1u, &descriptorSet->vkHandle(), 0u, nullptr);
    const VkBuffer vbo[1]{ (VkBuffer)meshVBO->Handle };
    constexpr static VkDeviceSize offsets[1]{ 0u };
    vkCmdBindVertexBuffers(cmd, 0u, 1u, vbo, offsets);
    vkCmdBindIndexBuffer(cmd, (VkBuffer)meshEBO->Handle, 0u, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, modelData->indexDataSize / sizeof(uint32_t), 1u, 0u, 0u, 0u);
}

void ContentCompilerScene::draw()
{
    constexpr static VkPipelineStageFlags wait_mask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    const VkSubmitInfo submit_info
    {
        VK_STRUCTURE_TYPE_SUBMIT_INFO,
        nullptr,
        1,
        &imageAcquireSemaphore->vkHandle(),
        &wait_mask,
        1u,
        &cmdPool->GetCmdBuffer(currentBuffer),
        1u,
        &renderCompleteSemaphore->vkHandle()
    };

    VkResult result = vkQueueSubmit(vprObjects.device->GraphicsQueue(), 1u, &submit_info, fences[currentBuffer]);
    VkAssert(result);
}

void ContentCompilerScene::endFrame()
{
    VkResult result = vkWaitForFences(vprObjects.device->vkHandle(), 1u, &fences[currentBuffer], VK_TRUE, UINT64_MAX);
    VkAssert(result);
    result = vkResetFences(vprObjects.device->vkHandle(), 1u, &fences[currentBuffer]);
    VkAssert(result);
}

void ContentCompilerScene::createUBO()
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
    constexpr resource_creation_flags uboFlags{ ResourceCreateMemoryStrategyMinFragmentation | ResourceCreateUserDataAsString | ResourceCreatePersistentlyMapped };
    constexpr const char* uboStr{ "HouseUBO" };
    auto& resourceContext = ResourceContext::Get();
    meshUBO = resourceContext.CreateBuffer(&ubo_info, nullptr, 0, nullptr, resource_usage::CPU_ONLY, uboFlags, (void*)uboStr);
}

void ContentCompilerScene::createMesh()
{
    const auto device = vprObjects.device;

    const uint32_t queueFamilyIndices[2]
    {
        device->QueueFamilyIndices().Graphics,
        device->QueueFamilyIndices().Transfer
    };

    const VkBufferCreateInfo vbo_info
    {
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        nullptr,
        0,
        static_cast<VkDeviceSize>(modelData->vertexDataSize),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        queueFamilyIndices[0] != queueFamilyIndices[1] ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE,
        queueFamilyIndices[0] != queueFamilyIndices[1] ? 2u : 0u,
        queueFamilyIndices[0] != queueFamilyIndices[1] ? queueFamilyIndices : nullptr
    };

    const gpu_resource_data_t vbo_data
    {
        modelData->vertexData,
        modelData->vertexDataSize,
        0u,
        VK_QUEUE_FAMILY_IGNORED
    };

    const VkBufferCreateInfo ebo_info
    {
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        nullptr,
        0,
        static_cast<VkDeviceSize>(modelData->indexDataSize),
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        queueFamilyIndices[0] != queueFamilyIndices[1] ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE,
        queueFamilyIndices[0] != queueFamilyIndices[1] ? 2u : 0u,
        queueFamilyIndices[0] != queueFamilyIndices[1] ? queueFamilyIndices : nullptr
    };
    
    const gpu_resource_data_t ebo_data
    {
        modelData->indexData,
        modelData->indexDataSize,
        0u,
        VK_QUEUE_FAMILY_IGNORED
    };

    constexpr resource_creation_flags creation_flags{ ResourceCreateMemoryStrategyMinFragmentation | ResourceCreateUserDataAsString };
    constexpr const char* vbo_str{ "ContentCompilerTestVBO" };
    constexpr const char* ebo_str{ "ContentCompilerTestEBO" };

    auto& resourceContext = ResourceContext::Get();
    meshVBO = resourceContext.CreateBuffer(&vbo_info, nullptr, 1u, &vbo_data, resource_usage::GPU_ONLY, creation_flags, (void*)vbo_str);
    meshEBO = resourceContext.CreateBuffer(&ebo_info, nullptr, 1u, &ebo_data, resource_usage::GPU_ONLY, creation_flags, (void*)ebo_str);

}

void ContentCompilerScene::createFences()
{

    constexpr static VkFenceCreateInfo fence_info
    {
        VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        nullptr,
        0u
    };

    fences.resize(vprObjects.swapchain->ImageCount());
    for (auto& fence : fences)
    {
        VkResult result = vkCreateFence(vprObjects.device->vkHandle(), &fence_info, nullptr, &fence);
        VkAssert(result);
    }
}

void ContentCompilerScene::createCommandPool()
{
    const VkCommandPoolCreateInfo pool_info
    {
        VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        nullptr,
        VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT | VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
        vprObjects.device->QueueFamilyIndices().Graphics
    };

    cmdPool = std::make_unique<vpr::CommandPool>(vprObjects.device->vkHandle(), pool_info);
}

void ContentCompilerScene::createDescriptorPool()
{
    descriptorPool = std::make_unique<vpr::DescriptorPool>(vprObjects.device->vkHandle(), 1);
    descriptorPool->AddResourceType(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1);
    descriptorPool->Create();
}

void ContentCompilerScene::createShaders()
{
    auto vertexHandle = st::CompileStandaloneShader("ccVertexShader", VK_SHADER_STAGE_VERTEX_BIT, vertexShaderSrc, strlen(vertexShaderSrc));
    size_t vertexBinarySz = 0u;
    st::RetrieveCompiledStandaloneShader(vertexHandle, &vertexBinarySz, nullptr);
    std::vector<uint32_t> vertexBinary(vertexBinarySz);
    st::RetrieveCompiledStandaloneShader(vertexHandle, &vertexBinarySz, vertexBinary.data());

    auto fragmentHandle = st::CompileStandaloneShader("ccFragmentShader", VK_SHADER_STAGE_FRAGMENT_BIT, fragmentShaderSrc, strlen(fragmentShaderSrc));
    size_t fragmentBinarySz = 0u;
    st::RetrieveCompiledStandaloneShader(fragmentHandle, &fragmentBinarySz, nullptr);
    std::vector<uint32_t> fragmentBinary(fragmentBinarySz);
    st::RetrieveCompiledStandaloneShader(fragmentHandle, &fragmentBinarySz, fragmentBinary.data());

    vertexShader = std::make_unique<vpr::ShaderModule>(vprObjects.device->vkHandle(), VK_SHADER_STAGE_VERTEX_BIT, vertexBinary.data(), vertexBinarySz);
    fragmentShader = std::make_unique<vpr::ShaderModule>(vprObjects.device->vkHandle(), VK_SHADER_STAGE_FRAGMENT_BIT, fragmentBinary.data(), fragmentBinarySz);
}

void ContentCompilerScene::createSetLayouts()
{
    constexpr static VkDescriptorSetLayoutBinding binding
    {
        0,
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        1,
        VK_SHADER_STAGE_VERTEX_BIT,
        nullptr
    };

    descriptorSetLayout = std::make_unique<vpr::DescriptorSetLayout>(vprObjects.device->vkHandle());
    descriptorSetLayout->AddDescriptorBinding(binding);
}

void ContentCompilerScene::createDescriptorSet()
{
    descriptorSet = std::make_unique<vpr::DescriptorSet>(vprObjects.device->vkHandle());
    descriptorSet->AddDescriptorInfo(VkDescriptorBufferInfo{ (VkBuffer)meshUBO->Handle, 0, sizeof(ubo_data_t) }, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0u);
    descriptorSet->Init(descriptorPool->vkHandle(), descriptorSetLayout->vkHandle());
}

void ContentCompilerScene::createPipelineLayout()
{
    const VkDescriptorSetLayout set_layouts[1]{ descriptorSetLayout->vkHandle() };
    pipelineLayout = std::make_unique<vpr::PipelineLayout>(vprObjects.device->vkHandle());
    pipelineLayout->Create(1u, set_layouts);
}

void ContentCompilerScene::createRenderpass()
{
    renderPass = CreateBasicRenderpass(vprObjects.device, vprObjects.swapchain, depthStencil.Format);
}

void ContentCompilerScene::createFramebuffers()
{
    framebuffers.resize(vprObjects.swapchain->ImageCount());
    for (size_t i = 0; i < framebuffers.size(); ++i)
    {
        std::array<VkImageView, 2> views
        {
            vprObjects.swapchain->ImageView(i),
            depthStencil.View
        };

        const VkFramebufferCreateInfo create_info
        {
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

void ContentCompilerScene::createPipeline()
{
}

void ContentCompilerScene::destroyFences()
{
    for (auto& fence : fences)
    {
        vkDestroyFence(vprObjects.device->vkHandle(), fence, nullptr);
    }
}

void ContentCompilerScene::destroyFramebuffers()
{
    for (auto& fbuff : framebuffers)
    {
        vkDestroyFramebuffer(vprObjects.device->vkHandle(), fbuff, nullptr);
    }
}
