#include "ContentCompilerScene.hpp"
#include "RenderingContext.hpp"
#include "ResourceContext.hpp"
#include "ResourceLoader.hpp"
#include "PipelineCache.hpp"
#include <string>
#include <filesystem>
#include <cassert>
#include "ObjModel.hpp"
#include "ObjMaterial.hpp"
#include "MeshProcessing.hpp"
#include "easylogging++.h"
INITIALIZE_EASYLOGGINGPP

namespace fs = std::filesystem;
ObjectModelData modelData;

static void BeginResizeCallback(VkSwapchainKHR handle, uint32_t width, uint32_t height)
{
    auto& loader = ResourceLoader::GetResourceLoader();
    loader.Stop();
    auto& transferSystem = ResourceContext::Get();
    transferSystem.Update();
    auto& scene = ContentCompilerScene::Get();
    scene.Destroy();
    auto& resourceContext = ResourceContext::Get();
    resourceContext.Destroy();
}

static void CompleteResizeCallback(VkSwapchainKHR handle, uint32_t width, uint32_t height)
{
    auto& renderingContext = RenderingContext::Get();
    auto& resourceContext = ResourceContext::Get();
    resourceContext.Initialize(renderingContext.Device(), renderingContext.PhysicalDevice(), VTF_VALIDATION_ENABLED);
    auto& scene = ContentCompilerScene::Get();
    scene.Construct(RequiredVprObjects{ renderingContext.Device(), renderingContext.PhysicalDevice(), renderingContext.Instance(), renderingContext.Swapchain() }, &modelData);
    auto& loader = ResourceLoader::GetResourceLoader();
    loader.Start(); // restart threads
}

void PostPhysicalDevicePreLogicalDeviceFunction(VkPhysicalDevice device, VkPhysicalDeviceFeatures** deviceFeatures, void** pNext)
{
    VkPhysicalDeviceDescriptorIndexingFeaturesEXT indexingFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES_EXT, nullptr };
    VkPhysicalDeviceFeatures2 deviceFeatures2{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &indexingFeatures };
    vkGetPhysicalDeviceFeatures2(device, &deviceFeatures2);

    if (indexingFeatures.descriptorBindingPartiallyBound && indexingFeatures.descriptorBindingVariableDescriptorCount)
    {
        VkPhysicalDeviceDescriptorIndexingFeaturesEXT* indexingFeaturesPtr = new VkPhysicalDeviceDescriptorIndexingFeaturesEXT(indexingFeatures);
        *pNext = indexingFeaturesPtr;
    }
}

void PostLogicalDeviceFunction(void* pNext)
{
    if (pNext)
    {
        VkPhysicalDeviceDescriptorIndexingFeaturesEXT* bindlessFeatures = reinterpret_cast<VkPhysicalDeviceDescriptorIndexingFeaturesEXT*>(pNext);
        delete bindlessFeatures;
    }
}

void EnableBindless(VkPhysicalDevice device)
{
}

int main(int argc, char* argv[])
{
    static const fs::path model_dir{ fs::canonical("../../../../assets/objs/bistro/") };
    assert(fs::exists(model_dir));
    static const std::string model_dir_str(model_dir.string());
    static const fs::path model_file{ model_dir / "exterior.obj" };
    assert(fs::exists(model_file));
    static const std::string model_file_str(model_file.string());

    ccDataHandle materialHandle = LoadMaterialFile("exterior.mtl", model_dir_str.c_str());
    ccDataHandle dataHandle = LoadObjModelFromFile(model_file_str.c_str(), model_dir_str.c_str(), RequiresNormals{ true }, RequiresTangents{ true }, OptimizeMesh{ false });
    CalculateTangents(dataHandle);
    //SortMeshMaterialRanges(dataHandle);
    modelData = RetrieveLoadedObjModel(dataHandle);

    static const fs::path curr_dir_path{ fs::current_path() / "shader_cache/" };
    if (!fs::exists(curr_dir_path))
    {
        fs::create_directories(curr_dir_path);
    }

    const std::string cacheDir = curr_dir_path.string();
    vpr::PipelineCache::SetCacheDirectory(cacheDir.c_str());

    auto& renderingContext = RenderingContext::Get();
    RenderingContext::AddSetupFunctions(&PostPhysicalDevicePreLogicalDeviceFunction, &PostLogicalDeviceFunction);
    renderingContext.Construct("RendererContextCfg.json");

    auto& resourceContext = ResourceContext::Get();
    resourceContext.Initialize(renderingContext.Device(), renderingContext.PhysicalDevice(), VTF_VALIDATION_ENABLED);

    auto& scene = ContentCompilerScene::Get();
    scene.Construct(RequiredVprObjects{ renderingContext.Device(), renderingContext.PhysicalDevice(), renderingContext.Instance(), renderingContext.Swapchain() }, &modelData);

    SwapchainCallbacks callbacks;
    callbacks.BeginResize = decltype(SwapchainCallbacks::BeginResize)::create<&BeginResizeCallback>();
    callbacks.CompleteResize = decltype(SwapchainCallbacks::CompleteResize)::create<&CompleteResizeCallback>();
    renderingContext.AddSwapchainCallbacks(callbacks);

    while (!renderingContext.ShouldWindowClose())
    {
        renderingContext.Update();
        resourceContext.Update();
        scene.Render(nullptr);
    }

    scene.Destroy();
    resourceContext.Destroy();
}