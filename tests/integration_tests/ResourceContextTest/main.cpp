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

static const fs::path BasePath{ fs::canonical("../../../../assets/ResourceContextTestAssets/") };
static const fs::path ObjFilePath = BasePath / fs::path("House.obj");
static const std::string HouseObjFile = ObjFilePath.string();
static const fs::path HouseTexturePath = BasePath / fs::path("House.png");
static const std::string HousePngFile = HouseTexturePath.string();
static const fs::path SkyboxTexturePath = BasePath / fs::path("Starbox.dds");
static const std::string SkyboxDdsFile = SkyboxTexturePath.string();

static bool dataLoadedToRAM = false;

static void objFileLoadedCallback(void* scene_ptr, void* data_ptr, void* user_data) {
    reinterpret_cast<VulkanComplexScene*>(scene_ptr)->CreateHouseMesh(data_ptr);
}

static void jpegLoadedCallback(void* scene_ptr, void* data_ptr, void* user_data) {
    reinterpret_cast<VulkanComplexScene*>(scene_ptr)->CreateHouseTexture(data_ptr);
}

static void skyboxLoadedCallback(void* scene_ptr, void* data, void* user_data) {
    reinterpret_cast<VulkanComplexScene*>(scene_ptr)->CreateSkyboxTexture(data);
}

static void BeginResizeCallback(VkSwapchainKHR handle, uint32_t width, uint32_t height) {
    auto& loader = ResourceLoader::GetResourceLoader();
    loader.Stop(); // get threads to finish pending work
    auto& transfer_sys = ResourceContext::Get();
    transfer_sys.Update(); // flush transfers before scene is killed
    auto& scene = VulkanComplexScene::GetScene();
    scene.Destroy();
    auto& rsrc = ResourceContext::Get();
    rsrc.Destroy();
}

static void CompleteResizeCallback(VkSwapchainKHR handle, uint32_t width, uint32_t height) {
    auto& scene = VulkanComplexScene::GetScene();
    auto& context = RenderingContext::Get();
    auto& rsrc = ResourceContext::Get();
    rsrc.Initialize(context.Device(), context.PhysicalDevice(), VTF_VALIDATION_ENABLED);
    scene.Construct(RequiredVprObjects{ context.Device(), context.PhysicalDevice(), context.Instance(), context.Swapchain() }, &rsrc);

    auto& loader = ResourceLoader::GetResourceLoader();
    loader.Start(); // restart threads

    auto& rsrc_loader = ResourceLoader::GetResourceLoader();
    rsrc_loader.Load("OBJ", HouseObjFile.c_str(), &scene, objFileLoadedCallback, nullptr);
    rsrc_loader.Load("PNG", HousePngFile.c_str(), &scene, jpegLoadedCallback, nullptr);
    rsrc_loader.Load("DDS", SkyboxDdsFile.c_str(), &scene, skyboxLoadedCallback, nullptr);
    dataLoadedToRAM = true;
}

int main(int argc, char* argv[]) {

    auto& context = RenderingContext::Get();
    context.Construct("RendererContextCfg.json");

    static const fs::path curr_dir_path{ fs::current_path() / "shader_cache/" };
    if (!fs::exists(curr_dir_path))
    {
        fs::create_directories(curr_dir_path);
    }

    const std::string cacheDir = curr_dir_path.string();
    vpr::PipelineCache::SetCacheDirectory(cacheDir.c_str());

    std::vector<fs::path> pathsToTest{ BasePath, ObjFilePath, HouseTexturePath, SkyboxTexturePath };

    for (auto& path : pathsToTest)
    {
        if (!fs::exists(path))
        {
            LOG(ERROR) << "Path " << path.string() << " does not exist!";
            throw std::runtime_error("Invalid paths.");
        }
    }

    auto& rsrc = ResourceContext::Get();
    rsrc.Initialize(context.Device(), context.PhysicalDevice(), VTF_VALIDATION_ENABLED);
    auto& rsrc_loader = ResourceLoader::GetResourceLoader();
    rsrc_loader.Subscribe("OBJ", &VulkanComplexScene::LoadObjFile, &VulkanComplexScene::DestroyObjFileData);
    rsrc_loader.Subscribe("PNG", &VulkanComplexScene::LoadPngImage, &VulkanComplexScene::DestroyPngFileData);
    rsrc_loader.Subscribe("DDS", &VulkanComplexScene::LoadCompressedTexture, &VulkanComplexScene::DestroyCompressedTextureData);

    auto& scene = VulkanComplexScene::GetScene();
    scene.Construct(RequiredVprObjects{ context.Device(), context.PhysicalDevice(), context.Instance(), context.Swapchain() }, &rsrc);

    rsrc_loader.Load("OBJ", HouseObjFile.c_str(), &scene, objFileLoadedCallback, nullptr);
    rsrc_loader.Load("PNG", HousePngFile.c_str(), &scene, jpegLoadedCallback, nullptr);
    rsrc_loader.Load("DDS", SkyboxDdsFile.c_str(), &scene, skyboxLoadedCallback, nullptr);
    dataLoadedToRAM = true;

    SwapchainCallbacks callbacks;
    callbacks.BeginResize = decltype(SwapchainCallbacks::BeginResize)::create<&BeginResizeCallback>();
    callbacks.CompleteResize = decltype(SwapchainCallbacks::CompleteResize)::create<&CompleteResizeCallback>();
    context.AddSwapchainCallbacks(callbacks);


    while (!context.ShouldWindowClose()) {
        context.Update();
        rsrc.Update();
        scene.Render(nullptr);
        /*if (dataLoadedToRAM)
        {
            rsrc_loader.Unload("OBJ", HouseObjFile.c_str());
            rsrc_loader.Unload("PNG", HousePngFile.c_str());
            rsrc_loader.Unload("DDS", SkyboxDdsFile.c_str());
            dataLoadedToRAM = false;
        }*/
    }


}
