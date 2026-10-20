#pragma once
#ifndef DIAMOND_DOGS_IMGUI_TEST_SCENE_HPP
#define DIAMOND_DOGS_IMGUI_TEST_SCENE_HPP
#include "VulkanScene.hpp"
#include <memory>
#include <vector>

namespace rhi
{
    class CommandPool;
}

class ImGuiModule;

/** Clears the swapchain image and draws the Dear ImGui demo window over it, through ImGuiModule. */
class ImGuiScene : public VulkanScene
{
public:
    ImGuiScene(rhi::RhiSystem* rhiSystem, PlatformWindowSystem* platformSystem);
    ~ImGuiScene();

    void Initialize(void* user_data) final;
    void Destroy() final;

protected:
    void update() final;
    void recordCommands() final;
    void draw() final;

    std::vector<std::unique_ptr<rhi::CommandPool>> commandPools;
    std::unique_ptr<ImGuiModule> imgui;
    bool setup{ false };
};

#endif // !DIAMOND_DOGS_IMGUI_TEST_SCENE_HPP
