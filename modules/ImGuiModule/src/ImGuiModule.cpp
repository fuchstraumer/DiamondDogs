#include "ImGuiModule.hpp"
#include "ImageDataFormatsVulkan.hpp"
#include "Device.hpp"
#include "Instance.hpp"
#include "RhiHandle.hpp"
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"
#include "vulkan/vulkan_core.h"
#include <GLFW/glfw3.h>
#include <cassert>
#include <cstdint>
#include <iostream>

namespace
{
    void CheckVkResult(VkResult result);
}

ImGuiModule::ImGuiModule(const ImGuiModuleCreateInfo& create_info)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    // no imgui.ini beside every executable that shows a debug window
    io.IniFilename = nullptr;
    ImGui::StyleColorsDark();

    GLFWwindow* window = static_cast<GLFWwindow*>(create_info.PlatformWindowHandle);
    const bool glfwInitialized = ImGui_ImplGlfw_InitForVulkan(window, create_info.InstallInputCallbacks);
    assert(glfwInitialized);

    const rhi::Device& device = *create_info.Device;
    // the backend deep-copies the format list, so a local is enough
    const VkFormat vkColorFormat = ToVkFormat(create_info.ColorFormat);

    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.ApiVersion = create_info.VulkanApiVersion;
    initInfo.Instance = create_info.Instance->Handle().As<VkInstance>();
    initInfo.PhysicalDevice = device.GetPhysicalDevice().As<VkPhysicalDevice>();
    initInfo.Device = device.Handle().As<VkDevice>();
    initInfo.QueueFamily = device.GetQueueFamilyIndices().Graphics;
    initInfo.Queue = device.GetGraphicsQueue(0).As<VkQueue>();
    // the backend creates and owns its descriptor pool
    initInfo.DescriptorPoolSize = IMGUI_IMPL_VULKAN_MINIMUM_IMAGE_SAMPLER_POOL_SIZE;
    initInfo.MinImageCount = create_info.MinImageCount;
    initInfo.ImageCount = create_info.ImageCount;
    initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    initInfo.UseDynamicRendering = true;
    initInfo.PipelineRenderingCreateInfo = VkPipelineRenderingCreateInfoKHR{ VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR };
    initInfo.PipelineRenderingCreateInfo.colorAttachmentCount = 1u;
    initInfo.PipelineRenderingCreateInfo.pColorAttachmentFormats = &vkColorFormat;
    initInfo.CheckVkResultFn = CheckVkResult;

    const bool vulkanInitialized = ImGui_ImplVulkan_Init(&initInfo);
    assert(vulkanInitialized);
}

ImGuiModule::~ImGuiModule()
{
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void ImGuiModule::BeginFrame()
{
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void ImGuiModule::Render(rhi::CommandBufferHandle cmd)
{
    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd.As<VkCommandBuffer>());
}

void ImGuiModule::SetMinImageCount(uint32_t min_image_count)
{
    ImGui_ImplVulkan_SetMinImageCount(min_image_count);
}

namespace
{
    void CheckVkResult(VkResult result)
    {
        if (result != VK_SUCCESS)
        {
            std::cerr << "ImGui Vulkan backend: VkResult " << static_cast<int32_t>(result) << "\n";
            assert(result >= VK_SUCCESS);
        }
    }
}
