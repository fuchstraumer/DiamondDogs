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

void DescriptorTest::createDescriptor() {
    descriptor = std::make_unique<Descriptor>("MainDescriptor", descriptorPool.get());

    descriptor->AddLayoutBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    descriptor->AddLayoutBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

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
