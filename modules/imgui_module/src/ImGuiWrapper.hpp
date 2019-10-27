#pragma once
#ifndef DIAMOND_DOGS_IMGUI_WRAPPER_HPP
#define DIAMOND_DOGS_IMGUI_WRAPPER_HPP
#include "ForwardDecl.hpp"
#include "imgui.h"
#include "GLFW/glfw3.h"
#include <array>
#include <chrono>
#include <vector>
#include "GraphicsPipeline.hpp"

class RenderingContext;
class ResourceContext;
struct RendererContext;
struct VulkanResource;


class ImGuiWrapper {
    ImGuiWrapper(const ImGuiWrapper&) = delete;
    ImGuiWrapper& operator=(const ImGuiWrapper&) = delete;
    ImGuiWrapper();
    ~ImGuiWrapper();
public:

    static ImGuiWrapper& GetImGuiWrapper();
    void Construct(VkRenderPass renderpass);
    void Destroy();
    void NewFrame();
    void EndImGuiFrame();
    void DrawFrame(size_t frame_idx, VkCommandBuffer cmd);

    inline static bool EnableMouseLocking = false;

private:

    struct ImGuiFrameData
    {
        VulkanResource* vbo{ nullptr };
        VulkanResource* ebo{ nullptr };
        VkDeviceSize currentVboSize = 0;
        VkDeviceSize currentEboSize = 0;
    };

    void newFrame();

    void updateBuffers(ImGuiFrameData* frame_data, const size_t frameIndex);

    void createResources();
    void loadFontData();
    void createFontImage();
    void createFontSampler();
    void createDescriptorSetLayout();
    void createDescriptorPool();
    void createDescriptorSet();
    void createPipelineLayout();
    void createShaders();
    void createCache();
    void createGraphicsPipeline(const VkRenderPass renderpass);
    void updateImguiSpecialKeys() noexcept;
    void freeMouse(GLFWwindow* instance);
    void captureMouse(GLFWwindow* instance);

    static float mouseWheel;
    std::array<bool, 3> mouseClick;
    const vpr::Device* device;
    RenderingContext* rendererContext;
    ResourceContext* resourceContext;

    VulkanResource* fontSampler;
    VulkanResource* fontImage;
    int imgWidth, imgHeight;
    std::vector<ImGuiFrameData> frameData;

    std::unique_ptr<vpr::PipelineCache> cache;
    std::unique_ptr<vpr::GraphicsPipeline> pipeline;
    std::unique_ptr<vpr::ShaderModule> vert, frag;
    std::unique_ptr<vpr::PipelineLayout> layout;
    std::unique_ptr<vpr::DescriptorSet> descriptorSet;
    std::unique_ptr<vpr::DescriptorSetLayout> setLayout;
    std::unique_ptr<vpr::DescriptorPool> descriptorPool;
    VkGraphicsPipelineCreateInfo pipelineCreateInfo;
    vpr::GraphicsPipelineInfo pipelineStateInfo;
    unsigned char* fontTextureData;
    std::chrono::high_resolution_clock::time_point timePointA;
    std::chrono::high_resolution_clock::time_point timePointB;
};

#endif // !VULPES_VK_IMGUI_WRAPPER_H
