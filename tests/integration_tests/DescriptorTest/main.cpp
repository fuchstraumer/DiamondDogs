#include "DescriptorTest.hpp"
#include "RenderingContext.hpp"
#include "ResourceContext.hpp"
#include "ResourceLoader.hpp"
#include <experimental/filesystem>
#include "GLFW/glfw3.h"
#include "easylogging++.h"
INITIALIZE_EASYLOGGINGPP

namespace fs = std::experimental::filesystem;

const std::string Skybox0_Path(fs::path(fs::current_path() / fs::path("Starbox.dds")).string());
const std::string Skybox1_Path(fs::path(fs::current_path() / fs::path("TestSkyboxBC3.dds")).string());

static void Texture0_Callback(void* instance, void* data) {
    reinterpret_cast<DescriptorTest*>(instance)->CreateSkyboxTexture0(data);
}

static void Texture1_Callback(void* instance, void* data) {
    reinterpret_cast<DescriptorTest*>(instance)->CreateSkyboxTexture1(data);
}

static void BeginResizeCallback(VkSwapchainKHR handle, uint32_t width, uint32_t height) {
    auto& scene = DescriptorTest::Get();
    scene.Destroy();
    auto& rsrc = ResourceContext::Get();
    rsrc.Destroy();
}

static void CompleteResizeCallback(VkSwapchainKHR handle, uint32_t width, uint32_t height) {
    auto& scene = DescriptorTest::Get();
    auto& context = RenderingContext::Get();
    auto& rsrc = ResourceContext::Get();
    rsrc.Construct(context.Device(), context.PhysicalDevice());
    scene.Construct(RequiredVprObjects{ context.Device(), context.PhysicalDevice(), context.Instance(), context.Swapchain() }, &rsrc);

    auto& rsrc_loader = ResourceLoader::GetResourceLoader();
    rsrc_loader.Load("DDS", Skybox0_Path.c_str(), &scene, Texture0_Callback, nullptr);
    rsrc_loader.Load("DDS", Skybox1_Path.c_str(), &scene, Texture1_Callback, nullptr);
}

static void KeyCallback(int key, int scancode, int action, int mods) {
    auto& scene = DescriptorTest::Get();
    if (key == GLFW_KEY_V) {
        scene.ChangeTexture();
    }
}

int main(int argc, char* argv[]) {
    auto& context = RenderingContext::Get();
    context.Construct("RendererContextCfg.json");

    auto& rsrc = ResourceContext::Get();
    rsrc.Construct(context.Device(), context.PhysicalDevice());
    auto& rsrc_loader = ResourceLoader::GetResourceLoader();
    rsrc_loader.Subscribe("DDS", &DescriptorTest::LoadCompressedTexture, &DescriptorTest::DestroyCompressedTexture);

    auto& scene = DescriptorTest::Get();
    scene.Construct(RequiredVprObjects{ context.Device(), context.PhysicalDevice(), context.Instance(), context.Swapchain() }, &rsrc);

    rsrc_loader.Load("DDS", Skybox0_Path.c_str(), &scene, Texture0_Callback, nullptr);
    rsrc_loader.Load("DDS", Skybox1_Path.c_str(), &scene, Texture1_Callback, nullptr);

    SwapchainCallbacks callbacks;
    callbacks.BeginResize = decltype(SwapchainCallbacks::BeginResize)::create<&BeginResizeCallback>();
    callbacks.CompleteResize = decltype(SwapchainCallbacks::CompleteResize)::create<&CompleteResizeCallback>();
    context.AddSwapchainCallbacks(callbacks);
    context.RegisterKeyboardKeyCallback(keyboard_key_callback_t::create<&KeyCallback>());

    while (!context.ShouldWindowClose()) {
        context.Update();
        rsrc.Update();
        scene.Render(nullptr);
    }

}
