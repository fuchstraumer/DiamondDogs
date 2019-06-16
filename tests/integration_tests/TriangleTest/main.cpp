#include "TriangleTest.hpp"
#include "RenderingContext.hpp"
#include "easylogging++.h"
INITIALIZE_EASYLOGGINGPP

static void BeginRecreateCallback(VkSwapchainKHR handle, uint32_t width, uint32_t height) {
    auto& tri = VulkanTriangle::GetScene();
    tri.Destroy();
}

static void CompleteResizeCallback(VkSwapchainKHR handle, uint32_t width, uint32_t height) {
    auto& tri = VulkanTriangle::GetScene();
    auto& context = RenderingContext::Get();
    RequiredVprObjects objects{
        context.Device(), context.PhysicalDevice(), context.Instance(), context.Swapchain()
    };
    tri.Construct(objects, nullptr);
}

int main(int argc, char* argv[]) {

    RenderingContext& renderer_context = RenderingContext::Get();
    renderer_context.Construct("RendererContextCfg.json");

    SwapchainCallbacks callbacks; 
    delegate_t<void(VkSwapchainKHR handle, uint32_t width, uint32_t height)> d;
    callbacks.BeginResize = decltype(d)::create<&BeginRecreateCallback>();
    callbacks.CompleteResize = decltype(d)::create<&CompleteResizeCallback>();
    renderer_context.AddSwapchainCallbacks(callbacks);

    auto& triangle = VulkanTriangle::GetScene();
    RequiredVprObjects objects{
        renderer_context.Device(), renderer_context.PhysicalDevice(), renderer_context.Instance(), renderer_context.Swapchain()
    };
    triangle.Construct(objects, nullptr);

    while (!renderer_context.ShouldWindowClose()) {
        renderer_context.Update();
        triangle.Render(nullptr);
    }

}
