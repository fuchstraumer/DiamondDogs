#include "ImGuiScene.hpp"
#include "CommandPool.hpp"
#include "Device.hpp"
#include "ImGuiModule.hpp"
#include "PlatformSystem.hpp"
#include "RhiAssert.hpp"
#include "RhiSystem.hpp"
#include "Swapchain.hpp"
#include "imgui.h"
#include <vulkan/vulkan_core.h>

ImGuiScene::ImGuiScene(rhi::RhiSystem* rhi_system, PlatformWindowSystem* platform_system) :
    VulkanScene(rhi_system, platform_system)
{}

ImGuiScene::~ImGuiScene()
{
    Destroy();
}

void ImGuiScene::Initialize(void* user_data)
{
    createFrameSyncObjects();

    // one pool per frame in flight, reset after beginFrame() waits on that frame's fence
    commandPools.reserve(numFramebuffers);
    for (uint32_t i = 0; i < numFramebuffers; ++i)
    {
        std::unique_ptr<rhi::CommandPool>& framePool = commandPools.emplace_back(
            std::make_unique<rhi::CommandPool>(device->Handle(), rhi::CommandPool::Type::Graphics, device->GetQueueFamilyIndices().Graphics));
        RhiAssert(framePool->AllocateCommandBuffers(1));
    }

    const ImGuiModuleCreateInfo createInfo
    {
        platformSystem->GetWindowHandle(),
        rhiSystem->GetInstance(),
        device,
        rhiSystem->GetVulkanApiVersion(),
        numFramebuffers,
        numFramebuffers,
        swapchain->ImageFormat(),
        true
    };
    imgui = std::make_unique<ImGuiModule>(createInfo);
    setup = true;
}

void ImGuiScene::Destroy()
{
    // main() calls this explicitly and the destructor calls it again, so the second call must be a no-op
    if (!setup)
    {
        return;
    }
    setup = false;

    vkDeviceWaitIdle(vkDevice);
    imgui.reset();
    destroyFrameSyncObjects();
    commandPools.clear();
}

void ImGuiScene::update()
{
    imgui->BeginFrame();
    ImGui::ShowDemoWindow();
    ImGui::Begin("DiamondDogs");
    ImGui::Text("Frame time: %.3f ms", 1000.0f / ImGui::GetIO().Framerate);
    ImGui::End();
}

void ImGuiScene::recordCommands()
{
    rhi::CommandPool& pool = *commandPools[currentFrame];
    RhiAssert(pool.Reset());
    const VkCommandBuffer cmd = pool.GetCommandBuffer(0).As<VkCommandBuffer>();

    constexpr static VkCommandBufferBeginInfo beginInfo
    {
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        nullptr,
        VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        nullptr
    };
    RhiAssert(rhi::Result(vkBeginCommandBuffer(cmd, &beginInfo)));

    const VkImage image = reinterpret_cast<VkImage>(swapchain->ImageHandle(currentAcquiredImage));
    const VkImageView imageView = reinterpret_cast<VkImageView>(swapchain->ImageViewHandle(currentAcquiredImage));
    const math::Float2 swapchainExtent = swapchain->GetExtent();
    const VkExtent2D extent{ static_cast<uint32_t>(swapchainExtent.x), static_cast<uint32_t>(swapchainExtent.y) };
    constexpr VkImageSubresourceRange colorRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

    // the attachment is cleared, so the old contents can be discarded and UNDEFINED is always a valid old layout
    const VkImageMemoryBarrier2 toAttachment
    {
        VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        nullptr,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        0,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_QUEUE_FAMILY_IGNORED,
        VK_QUEUE_FAMILY_IGNORED,
        image,
        colorRange
    };
    const VkDependencyInfo toAttachmentDependency{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO, nullptr, 0, 0, nullptr, 0, nullptr, 1, &toAttachment };
    vkCmdPipelineBarrier2(cmd, &toAttachmentDependency);

    const VkRenderingAttachmentInfo colorAttachment
    {
        VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        nullptr,
        imageView,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_RESOLVE_MODE_NONE,
        VK_NULL_HANDLE,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_ATTACHMENT_LOAD_OP_CLEAR,
        VK_ATTACHMENT_STORE_OP_STORE,
        VkClearValue{ VkClearColorValue{ 0.05f, 0.05f, 0.08f, 1.0f } }
    };
    const VkRenderingInfo renderingInfo
    {
        VK_STRUCTURE_TYPE_RENDERING_INFO,
        nullptr,
        0,
        VkRect2D{ VkOffset2D{ 0, 0 }, extent },
        1,
        0,
        1,
        &colorAttachment,
        nullptr,
        nullptr
    };
    vkCmdBeginRendering(cmd, &renderingInfo);
    imgui->Render(rhi::CommandBufferHandle{ reinterpret_cast<uint64_t>(cmd) });
    vkCmdEndRendering(cmd);

    const VkImageMemoryBarrier2 toPresent
    {
        VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        nullptr,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        // the present semaphore is signalled at this stage. with NONE here, the signal did not cover the layout
        // transition, and sync validation reported PRESENT_AFTER_WRITE
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        0,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_QUEUE_FAMILY_IGNORED,
        VK_QUEUE_FAMILY_IGNORED,
        image,
        colorRange
    };
    const VkDependencyInfo toPresentDependency{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO, nullptr, 0, 0, nullptr, 0, nullptr, 1, &toPresent };
    vkCmdPipelineBarrier2(cmd, &toPresentDependency);

    RhiAssert(rhi::Result(vkEndCommandBuffer(cmd)));
}

void ImGuiScene::draw()
{
    const VkSemaphoreSubmitInfo waitInfo
    {
        VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        nullptr,
        imageAvailableSemaphores[currentFrame],
        0,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        0
    };
    const VkSemaphoreSubmitInfo signalInfo
    {
        VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        nullptr,
        renderFinishedSemaphores[currentFrame],
        0,
        VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
        0
    };
    const VkCommandBufferSubmitInfo cmdInfo
    {
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        nullptr,
        commandPools[currentFrame]->GetCommandBuffer(0).As<VkCommandBuffer>(),
        0
    };
    const VkSubmitInfo2 submitInfo
    {
        VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        nullptr,
        0,
        1,
        &waitInfo,
        1,
        &cmdInfo,
        1,
        &signalInfo
    };
    const VkQueue queue = device->GetGraphicsQueue(0).As<VkQueue>();
    RhiAssert(rhi::Result(vkQueueSubmit2(queue, 1, &submitInfo, inFlightFences[currentFrame])));
}
