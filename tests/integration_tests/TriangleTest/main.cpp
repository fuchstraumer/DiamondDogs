#include "TriangleTest.hpp"
#include "RenderingContext.hpp"

static void SwapchainCreatedCallback(VkSwapchainKHR swapchain, uint32_t width, uint32_t height, void* user_data)
{
}

static void BeginRecreateCallback(VkSwapchainKHR handle, uint32_t width, uint32_t height, void* user_data)
{
    auto& tri = VulkanTriangle::GetScene();
    tri.Destroy();
}

static void CompleteResizeCallback(VkSwapchainKHR handle, uint32_t width, uint32_t height, void* user_data)
{
    auto& tri = VulkanTriangle::GetScene();
    auto& context = RenderingContext::Get();
    RequiredVprObjects objects
    {
        context.Device(), context.PhysicalDevice(), context.Instance(), context.Swapchain()
    };
    tri.Construct(objects, nullptr);
}

static void SwapchainDestroyedCallback(VkSwapchainKHR swapchain, void* user_data)
{
}

int main(int argc, char* argv[])
{

    RenderingContext& renderer_context = RenderingContext::Get();
    renderer_context.Construct("RendererContextCfg.json");

    SwapchainCallbacks callbacks;
    callbacks.SwapchainCreated = SwapchainCreatedCallbackType::create<&SwapchainCreatedCallback>();
    callbacks.BeginResize = SwapchainBeginResizeCallbackType::create<&BeginRecreateCallback>();
    callbacks.CompleteResize = SwapchainCompleteResizeCallbackType::create<&CompleteResizeCallback>();
    callbacks.SwapchainDestroyed = SwapchainDestroyedCallbackType::create<&SwapchainDestroyedCallback>();
    renderer_context.AddSwapchainCallbacks(callbacks);

    auto& triangle = VulkanTriangle::GetScene();
    RequiredVprObjects objects
    {
        renderer_context.Device(), renderer_context.PhysicalDevice(), renderer_context.Instance(), renderer_context.Swapchain()
    };
    triangle.Construct(objects, nullptr);

    while (!renderer_context.ShouldWindowClose())
    {
        renderer_context.Update();
        triangle.Render(nullptr);
    }

}
