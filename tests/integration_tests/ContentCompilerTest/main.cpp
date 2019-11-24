#include "ContentCompilerScene.hpp"
#include "RenderingContext.hpp"
#include "ResourceContext.hpp"
#include "ResourceLoader.hpp"
#include "PipelineCache.hpp"
#include <string>
#include <filesystem>
#include <cassert>
#include "ObjModel.hpp"
#include "easylogging++.h"
INITIALIZE_EASYLOGGINGPP

namespace fs = std::filesystem;

static void BeginResizeCallback(VkSwapchainKHR handle, uint32_t width, uint32_t height)
{
    auto& loader = ResourceLoader::GetResourceLoader();
    loader.Stop();
    auto& transferSystem = ResourceContext::Get();
    transferSystem.Update();
    
}

int main(int argc, char* argv[])
{
    static const fs::path model_dir{ fs::canonical("../../../../assets/objs/bistro/") };
    assert(fs::exists(model_dir));
    static const std::string model_dir_str(model_dir.string());
    static const fs::path model_file{ model_dir / "exterior.obj" };
    assert(fs::exists(model_file));
    static const std::string model_file_str(model_file.string());


    ccDataHandle dataHandle = LoadObjModelFromFile(model_file_str.c_str(), model_dir_str.c_str(), RequiresNormals{ true }, RequiresTangents{ true }, OptimizeMesh{ false });
}