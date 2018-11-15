#include "DescriptorTest.hpp"
#include "ResourceContext.hpp"
#include "glm/vec3.hpp"
#include "SkyboxShaders.hpp"
#include "gli/gli.hpp"
#include "vkAssert.hpp"
#include "PipelineCache.hpp"
#include "DescriptorPool.hpp"
#include "PipelineLayout.hpp"
#include "ShaderModule.hpp"
#include "LogicalDevice.hpp"
#include "Swapchain.hpp"
#include "PhysicalDevice.hpp"
#include "CommandPool.hpp"
#include "Semaphore.hpp"
#include "ResourceTypes.hpp"
#include "CreateInfoBase.hpp"

const static std::array<glm::vec3, 8> skybox_positions{
    glm::vec3(-1.0f,-1.0f, 1.0f),
    glm::vec3( 1.0f,-1.0f, 1.0f),
    glm::vec3( 1.0f, 1.0f, 1.0f),
    glm::vec3(-1.0f, 1.0f, 1.0f),
    glm::vec3( 1.0f,-1.0f,-1.0f),
    glm::vec3(-1.0f,-1.0f,-1.0f),
    glm::vec3(-1.0f, 1.0f,-1.0f),
    glm::vec3( 1.0f, 1.0f,-1.0f)
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

DescriptorTest::DescriptorTest() {}

DescriptorTest::~DescriptorTest() {
    Destroy();
}

DescriptorTest& DescriptorTest::Get() {
    static DescriptorTest dsc;
    return dsc;
}

void DescriptorTest::Construct(RequiredVprObjects objects, void * user_data) {
    vprObjects = objects;
    resourceContext = reinterpret_cast<ResourceContext*>(user_data);
    skyboxUboData.view = glm::mat3(glm::lookAt(glm::vec3(-2.0f, -2.0f, 1.0f), glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f)));
    skyboxUboData.projection = glm::perspectiveFov(glm::radians(70.0f), static_cast<float>(objects.swapchain->Extent().width), static_cast<float>(objects.swapchain->Extent().height), 0.1f, 1000.0f);
    skyboxUboData.projection[1][1] *= -1.0f;
    skyboxUboData.model = glm::mat4(1.0f);
    createSemaphores();
    createSampler();
    createUBO();
    update();
    createFences();
    createCommandPool();
    createSkyboxMesh();
    createDescriptorPool();
    createShaders();
    createDescriptor();
    createPipelineLayout();
    createDepthStencil();
    createRenderpass();
    createFramebuffers();
    createPipeline();
}

void DescriptorTest::Destroy() {
    resourceContext->DestroyResource(sampler);
    resourceContext->DestroyResource(skyboxEBO);
    resourceContext->DestroyResource(skyboxVBO);
    resourceContext->DestroyResource(skyboxUBO);
    resourceContext->DestroyResource(skyboxTexture0);
    resourceContext->DestroyResource(skyboxTexture1);
    descriptor.reset();
    pipelineLayout.reset();
    vert.reset();
    frag.reset();
    imageAcquireSemaphore.reset();
    renderCompleteSemaphore.reset();
    destroyFences();
    destroyFramebuffers();
    vkDestroyRenderPass(vprObjects.device->vkHandle(), renderPass, nullptr);
    vkFreeMemory(vprObjects.device->vkHandle(), depthStencil.Memory, nullptr);
    vkDestroyImageView(vprObjects.device->vkHandle(), depthStencil.View, nullptr);
    vkDestroyImage(vprObjects.device->vkHandle(), depthStencil.Image, nullptr);
    texture0Ready = false;
    texture1Ready = false;
    texture0Bound = false;
}

void* DescriptorTest::LoadCompressedTexture(const char * fname, void * user_data) {
    return new gli::texture_cube(gli::load(fname));
}

void DescriptorTest::DestroyCompressedTexture(void * texture) {
    gli::texture_cube* cube = reinterpret_cast<gli::texture_cube*>(texture);
    delete cube;
}

void DescriptorTest::CreateSkyboxTexture0(void * texture_data) {
    skyboxTexture0 = createSkyboxTexture(texture_data);
    texture0Ready = true;
    if (!texture1Ready) {
        descriptor->BindCombinedImageSampler(1, skyboxTexture0, sampler);
        texture0Bound = true;
    }
}

void DescriptorTest::CreateSkyboxTexture1(void * texture_data) {
    skyboxTexture1 = createSkyboxTexture(texture_data);
    texture1Ready = true;
    if (!texture0Ready) {
        descriptor->BindCombinedImageSampler(1, skyboxTexture1, sampler);
        texture0Bound = false;
    }
}

void DescriptorTest::ChangeTexture() {
    if (texture0Bound) {
        descriptor->BindCombinedImageSampler(1, skyboxTexture1, sampler);
        texture0Bound = false;
    }
    else {
        descriptor->BindCombinedImageSampler(1, skyboxTexture0, sampler);
        texture0Bound = true;
    }
}

VulkanResource* DescriptorTest::createSkyboxTexture(void* texture_data){ 
    gli::texture_cube* texture = reinterpret_cast<gli::texture_cube*>(texture_data);
    const uint32_t width = static_cast<uint32_t>(texture->extent().x);
    const uint32_t height = static_cast<uint32_t>(texture->extent().y);
    const uint32_t mipLevels = static_cast<uint32_t>(texture->levels());

    const VkImageCreateInfo image_info{
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
        VK_SHARING_MODE_EXCLUSIVE,
        0,
        nullptr,
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
                uint32_t(1),
                static_cast<uint32_t>(j)
            });
        }
    }

    return resourceContext->CreateImage(&image_info, &view_info, image_copies.size(), image_copies.data(), memory_type::DEVICE_LOCAL, nullptr);
}

void DescriptorTest::update() {
    const gpu_resource_data_t skybox_ubo_data{
        &skyboxUboData,
        sizeof(ubo_data_t),
        0,
        0,
        0
    };
    resourceContext->SetBufferData(skyboxUBO, 1, &skybox_ubo_data);
}

void DescriptorTest::recordCommands() {
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
        if (texture0Ready || texture1Ready) {
            vkCmdSetViewport(pool[currentBuffer], 0, 1, &viewport);
            vkCmdSetScissor(pool[currentBuffer], 0, 1, &scissor);
            renderSkybox(pool[currentBuffer]);
        }
        vkCmdEndRenderPass(pool[currentBuffer]);
        result = vkEndCommandBuffer(pool[currentBuffer]);
        VkAssert(result);
    }

    first_frame[currentBuffer] = false;
}

void DescriptorTest::renderSkybox(VkCommandBuffer cmd) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    VkDescriptorSet set = descriptor->Handle();
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout->vkHandle(), 0, 1, &set, 0, nullptr);
    const VkBuffer buffer[1]{ (VkBuffer)skyboxVBO->Handle };
    constexpr static VkDeviceSize offsets[1]{ 0 };
    vkCmdBindVertexBuffers(cmd, 0, 1, buffer, offsets);
    vkCmdBindIndexBuffer(cmd, (VkBuffer)skyboxEBO->Handle, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, skyboxIndexCount, 1, 0, 0, 0);
}

void DescriptorTest::draw() {
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

void DescriptorTest::endFrame() {
    VkResult result = vkWaitForFences(vprObjects.device->vkHandle(), 1, &fences[currentBuffer], VK_TRUE, UINT64_MAX);
    VkAssert(result);
    result = vkResetFences(vprObjects.device->vkHandle(), 1, &fences[currentBuffer]);
    VkAssert(result);
}

void DescriptorTest::createSampler() {
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
    sampler = resourceContext->CreateSampler(&sampler_info, nullptr);
}

void DescriptorTest::createDepthStencil() {
    depthStencil = CreateDepthStencil(vprObjects.device, vprObjects.physicalDevice, vprObjects.swapchain);
}

void DescriptorTest::createUBO() {
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
    skyboxUBO = resourceContext->CreateBuffer(&ubo_info, nullptr, 0, nullptr, memory_type::HOST_VISIBLE, nullptr);
}

void DescriptorTest::createSkyboxMesh() {
    skybox_mesh_data_t mesh_data;

    const VkBufferCreateInfo vbo_info{
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        nullptr,
        0,
        static_cast<VkDeviceSize>(sizeof(glm::vec3) * mesh_data.vertices.size()),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_SHARING_MODE_EXCLUSIVE,
        0,
        nullptr
    };

    const VkBufferCreateInfo ebo_info{
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        nullptr,
        0,
        static_cast<VkDeviceSize>(sizeof(uint32_t) * mesh_data.indices.size()),
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_SHARING_MODE_EXCLUSIVE,
        0,
        nullptr
    };

    const gpu_resource_data_t vbo_data{
        mesh_data.vertices.data(),
        vbo_info.size,
        0,
        0,
        0
    };

    const gpu_resource_data_t ebo_data{
        mesh_data.indices.data(),
        ebo_info.size,
        0,
        0,
        0
    };

    skyboxVBO = resourceContext->CreateBuffer(&vbo_info, nullptr, 1, &vbo_data, memory_type::DEVICE_LOCAL, nullptr);
    skyboxEBO = resourceContext->CreateBuffer(&ebo_info, nullptr, 1, &ebo_data, memory_type::DEVICE_LOCAL, nullptr);
    skyboxIndexCount = static_cast<uint32_t>(mesh_data.indices.size());
}

void DescriptorTest::createFences(){
    constexpr static VkFenceCreateInfo fence_info{
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

void DescriptorTest::createCommandPool() {
    const VkCommandPoolCreateInfo create_info{
        VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        nullptr,
        VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT | VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
        vprObjects.device->QueueFamilyIndices().Graphics
    };
    cmdPool = std::make_unique<vpr::CommandPool>(vprObjects.device->vkHandle(), create_info);
    cmdPool->AllocateCmdBuffers(vprObjects.swapchain->ImageCount(), VK_COMMAND_BUFFER_LEVEL_PRIMARY);
}

void DescriptorTest::createDescriptorPool() {
    descriptorPool = std::make_unique<vpr::DescriptorPool>(vprObjects.device->vkHandle(), 3);
    descriptorPool->AddResourceType(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2);
    descriptorPool->AddResourceType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2);
    descriptorPool->Create();
}

static VkImageCreateInfo dummy_img_info{
    VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
    nullptr,
    0,
    VK_IMAGE_TYPE_2D,
    VK_FORMAT_UNDEFINED,
    VkExtent3D{ 0, 0, 1 },
    1,
    1,
    VK_SAMPLE_COUNT_1_BIT,
    VK_IMAGE_TILING_LINEAR,
    VK_IMAGE_USAGE_SAMPLED_BIT,
    VK_SHARING_MODE_EXCLUSIVE,
    0,
    nullptr,
    VK_IMAGE_LAYOUT_UNDEFINED,
};

static VulkanResource empty_img_resource{
    resource_type::IMAGE,
    VK_NULL_HANDLE,
    reinterpret_cast<void*>(&dummy_img_info),
    VK_NULL_HANDLE,
    nullptr,
    nullptr,
    nullptr
};

void DescriptorTest::createDescriptor() {
    descriptor = std::make_unique<Descriptor>("MainDescriptor", descriptorPool.get());

    descriptor->AddLayoutBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    descriptor->AddLayoutBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    descriptor->BindResourceToIdx(0, skyboxUBO);
    descriptor->BindCombinedImageSampler(1, &empty_img_resource, sampler);

}

void DescriptorTest::createPipelineLayout() {
    const VkDescriptorSetLayout set_layouts[1]{ descriptor->SetLayout() };
    pipelineLayout = std::make_unique<vpr::PipelineLayout>(vprObjects.device->vkHandle());
    pipelineLayout->Create(1, set_layouts);
}

void DescriptorTest::createShaders() {
    vert = std::make_unique<vpr::ShaderModule>(vprObjects.device->vkHandle(), VK_SHADER_STAGE_VERTEX_BIT, skybox_shader_vert_spv, sizeof(skybox_shader_vert_spv));
    frag = std::make_unique<vpr::ShaderModule>(vprObjects.device->vkHandle(), VK_SHADER_STAGE_FRAGMENT_BIT, skybox_shader_frag_spv, sizeof(skybox_shader_frag_spv));
}

void DescriptorTest::createRenderpass() {
    renderPass = CreateBasicRenderpass(vprObjects.device, vprObjects.swapchain, depthStencil.Format);
}

void DescriptorTest::createFramebuffers() {
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

void DescriptorTest::createPipeline() {
    const VkPipelineShaderStageCreateInfo shader_stages[2]{
        vert->PipelineInfo(),
        frag->PipelineInfo()
    };

    constexpr static VkVertexInputBindingDescription vertex_bindings[1]{
        VkVertexInputBindingDescription{ 0, sizeof(glm::vec3), VK_VERTEX_INPUT_RATE_VERTEX }
    };

    constexpr static VkVertexInputAttributeDescription vertex_attr[1]{
        VkVertexInputAttributeDescription{ 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 }
    };

    constexpr static VkPipelineVertexInputStateCreateInfo vertex_info{
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        nullptr,
        0,
        1,
        vertex_bindings,
        1,
        vertex_attr
    };

    pipelineCache = std::make_unique<vpr::PipelineCache>(vprObjects.device->vkHandle(), vprObjects.physicalDevice->vkHandle(), typeid(DescriptorTest).hash_code() + std::hash<std::string>()("SkyboxCache"));
    pipeline = CreateBasicPipeline(vprObjects.device, 2, shader_stages, &vertex_info, pipelineLayout->vkHandle(), renderPass, VK_COMPARE_OP_LESS_OR_EQUAL, pipelineCache->vkHandle(), VK_NULL_HANDLE);
}

void DescriptorTest::destroyFences() {
    for (auto& fence : fences) {
        vkDestroyFence(vprObjects.device->vkHandle(), fence, nullptr);
    }
}

void DescriptorTest::destroyFramebuffers() {
    for (auto& fbuff : framebuffers) {
        vkDestroyFramebuffer(vprObjects.device->vkHandle(), fbuff, nullptr);
    }
}
