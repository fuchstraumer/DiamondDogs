#include "ImGuiScene.hpp"
#include "CommandBuffer.hpp"
#include "CommandPool.hpp"
#include "Device.hpp"
#include "ImGuiModule.hpp"
#include "PlatformSystem.hpp"
#include "RhiAssert.hpp"
#include "RhiSystem.hpp"
#include "RhiTypes.hpp"
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

    device->WaitDeviceIdle();
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
    rhi::CommandBuffer cmd(pool.GetCommandBuffer(0));
    RhiAssert(cmd.Begin());

    const rhi::ImageHandle image{ reinterpret_cast<uint64_t>(swapchain->ImageHandle(currentAcquiredImage)) };
    const math::Float2 swapchainExtent = swapchain->GetExtent();
    constexpr rhi::ImageSubresourceRange k_ColorRange{ rhi::ImageAspect::Color, 0, 1, 0, 1 };

    // the attachment is cleared, so the old contents can be discarded and UNDEFINED is always a valid old layout
    const rhi::ImageBarrier toAttachment
    {
        image,
        rhi::PipelineStage::ColorAttachmentOutput, rhi::Access::None,
        rhi::PipelineStage::ColorAttachmentOutput, rhi::Access::ColorAttachmentRead | rhi::Access::ColorAttachmentWrite,
        rhi::ImageLayout::Undefined, rhi::ImageLayout::ColorAttachment,
        k_ColorRange
    };
    cmd.Barrier(rhi::DependencyInfo{ {}, {}, std::span{ &toAttachment, 1 } });

    rhi::ColorAttachment colorAttachment{};
    colorAttachment.ImageView = rhi::ImageViewHandle{ reinterpret_cast<uint64_t>(swapchain->ImageViewHandle(currentAcquiredImage)) };
    colorAttachment.ClearValue = rhi::ClearColorValue{ 0.05f, 0.05f, 0.08f, 1.0f };
    rhi::RenderingInfo renderingInfo{};
    renderingInfo.ColorAttachments = std::span{ &colorAttachment, 1 };
    renderingInfo.RenderArea = rhi::Rect2D{ 0, 0, static_cast<uint32_t>(swapchainExtent.x), static_cast<uint32_t>(swapchainExtent.y) };
    cmd.BeginRendering(renderingInfo);
    imgui->Render(cmd.Handle());
    cmd.EndRendering();

    const rhi::ImageBarrier toPresent
    {
        image,
        rhi::PipelineStage::ColorAttachmentOutput, rhi::Access::ColorAttachmentWrite,
        // the present semaphore is signalled at this stage. with NONE here, the signal did not cover the layout
        // transition, and sync validation reported PRESENT_AFTER_WRITE
        rhi::PipelineStage::ColorAttachmentOutput, rhi::Access::None,
        rhi::ImageLayout::ColorAttachment, rhi::ImageLayout::PresentSource,
        k_ColorRange
    };
    cmd.Barrier(rhi::DependencyInfo{ {}, {}, std::span{ &toPresent, 1 } });

    RhiAssert(cmd.End());
}

void ImGuiScene::draw()
{
    submitFrame(commandPools[currentFrame]->GetCommandBuffer(0));
}
