#include "ComplexScene.hpp"
#include "RenderingContext.hpp"
#include "ResourceContext.hpp"
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

static void BeginResizeCallback(uint64_t handle, uint32_t width, uint32_t height) {
    auto& scene = VulkanComplexScene::GetScene();
    scene.Destroy();
}

static void CompleteResizeCallback(uint64_t handle, uint32_t width, uint32_t height) {
    auto& scene = VulkanComplexScene::GetScene();
    auto& context = RenderingContext::Get();
    scene.Construct(RequiredVprObjects{ context.Device(), context.PhysicalDevice(), context.Instance(), context.Swapchain() }, resource_api);
    resource_api->LoadFile("OBJ", HouseObjFile.c_str(), &scene, objFileLoadedCallback, nullptr);
    resource_api->LoadFile("JPEG", HousePngFile.c_str(), &scene, jpegLoadedCallback, nullptr);
    resource_api->LoadFile("DDS", SkyboxDdsFile.c_str(), &scene, skyboxLoadedCallback, nullptr);
}

int main(int argc, char* argv[]) {

    auto& scene = VulkanComplexScene::GetScene();
    scene.Construct(RequiredVprObjects{ context->LogicalDevice, context->PhysicalDevices[0], context->VulkanInstance, context->Swapchain }, resource_api);
    resource_api->LoadFile("OBJ", HouseObjFile.c_str(), &scene, objFileLoadedCallback, nullptr);
    resource_api->LoadFile("JPEG", HousePngFile.c_str(), &scene, jpegLoadedCallback, nullptr);
    resource_api->LoadFile("DDS", SkyboxDdsFile.c_str(), &scene, skyboxLoadedCallback, nullptr);

    SwapchainCallbacks_API callbacks{ nullptr };
    callbacks.BeginSwapchainResize = BeginResizeCallback;
    callbacks.CompleteSwapchainResize = CompleteResizeCallback;
    renderer_api->RegisterSwapchainCallbacks(&callbacks);

    Plugin_API* renderer_context_core_api = reinterpret_cast<Plugin_API*>(manager.RetrieveBaseAPI(RENDERER_CONTEXT_API_ID));
    //scene.WaitForAllLoaded();
    static bool assets_loaded = false;

    while (!renderer_api->WindowShouldClose()) {
        renderer_context_core_api->LogicalUpdate();
        resource_api->CompletePendingTransfers();
        scene.Render(nullptr);
    }


}
