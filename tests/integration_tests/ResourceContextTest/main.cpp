#include "ComplexScene.hpp"
#include "RenderingContext.hpp"
#include "ResourceContext.hpp"
#include "ResourceLoader.hpp"
#include "PipelineCache.hpp"
#include "easylogging++.h"
INITIALIZE_EASYLOGGINGPP

#ifndef __APPLE_CC__
#include <experimental/filesystem>
namespace fs = std::experimental::filesystem;
#else
#include <boost/filesystem.hpp>
namespace fs = boost::filesystem;
#endif

const std::string HouseObjFile(fs::path(fs::current_path() / fs::path("House.obj")).string());
const std::string HousePngFile(fs::path(fs::current_path() / fs::path("House.png")).string());
const std::string SkyboxDdsFile(fs::path(fs::current_path() / fs::path("Starbox.dds")).string());

static void objFileLoadedCallback(void* scene_ptr, void* data_ptr) {
    reinterpret_cast<VulkanComplexScene*>(scene_ptr)->CreateHouseMesh(data_ptr);
}

static void jpegLoadedCallback(void* scene_ptr, void* data_ptr) {
    reinterpret_cast<VulkanComplexScene*>(scene_ptr)->CreateHouseTexture(data_ptr);
}

static void skyboxLoadedCallback(void* scene_ptr, void* data) {
    reinterpret_cast<VulkanComplexScene*>(scene_ptr)->CreateSkyboxTexture(data);
}

static void BeginResizeCallback(VkSwapchainKHR handle, uint32_t width, uint32_t height) {
    auto& scene = VulkanComplexScene::GetScene();
    scene.Destroy();
}

static void CompleteResizeCallback(VkSwapchainKHR handle, uint32_t width, uint32_t height) {
    auto& scene = VulkanComplexScene::GetScene();
    auto& context = RenderingContext::Get();
    auto& rsrc = ResourceContext::Get();
    scene.Construct(RequiredVprObjects{ context.Device(), context.PhysicalDevice(), context.Instance(), context.Swapchain() }, &rsrc);

    auto& rsrc_loader = ResourceLoader::GetResourceLoader();
    rsrc_loader.Load("OBJ", HouseObjFile.c_str(), &scene, objFileLoadedCallback, nullptr);
    rsrc_loader.Load("PNG", HousePngFile.c_str(), &scene, jpegLoadedCallback, nullptr);
    rsrc_loader.Load("DDS", SkyboxDdsFile.c_str(), &scene, skyboxLoadedCallback, nullptr);
}

int main(int argc, char* argv[]) {

    auto& context = RenderingContext::Get();
    context.Construct("RendererContextCfg.json");

    auto& rsrc = ResourceContext::Get();
    rsrc.Construct(context.Device(), context.PhysicalDevice());
    auto& rsrc_loader = ResourceLoader::GetResourceLoader();
    rsrc_loader.Subscribe("OBJ", &VulkanComplexScene::LoadObjFile, &VulkanComplexScene::DestroyObjFileData);
    rsrc_loader.Subscribe("PNG", &VulkanComplexScene::LoadPngImage, &VulkanComplexScene::DestroyPngFileData);
    rsrc_loader.Subscribe("DDS", &VulkanComplexScene::LoadCompressedTexture, &VulkanComplexScene::DestroyCompressedTextureData);

    auto& scene = VulkanComplexScene::GetScene();
    scene.Construct(RequiredVprObjects{ context.Device(), context.PhysicalDevice(), context.Instance(), context.Swapchain() }, &rsrc);

    rsrc_loader.Load("OBJ", HouseObjFile.c_str(), &scene, objFileLoadedCallback, nullptr);
    rsrc_loader.Load("PNG", HousePngFile.c_str(), &scene, jpegLoadedCallback, nullptr);
    rsrc_loader.Load("DDS", SkyboxDdsFile.c_str(), &scene, skyboxLoadedCallback, nullptr);

    SwapchainCallbacks callbacks{ nullptr };
    callbacks.BeginResize = BeginResizeCallback;
    callbacks.CompleteResize = CompleteResizeCallback;
    context.AddSwapchainCallbacks(callbacks);

    while (!context.ShouldWindowClose()) {
        context.Update();
        rsrc.Update();
        scene.Render(nullptr);
        rsrc.FlushStagingBuffers();
    }


}
