#include "VTF_Scene.hpp"
#include "core/ShaderPack.hpp"
#include "RenderingContext.hpp"
#include "ResourceContext.hpp"
#include "Swapchain.hpp"
#include "core/ShaderPack.hpp"
#include "generation/ShaderGenerator.hpp"
#include "easylogging++.h"
#include <experimental/filesystem>
INITIALIZE_EASYLOGGINGPP
#include "glm/gtc/matrix_transform.hpp"
#include "../third_party/shadertools/src/util/ShaderPackBinary.hpp"
#include "PipelineCache.hpp"
#include "Allocator.hpp"
#include "Instance.hpp"
#include "CommandPool.hpp"

static int screen_x() {
    auto& ctxt = RenderingContext::Get();
    return static_cast<int>(ctxt.Swapchain()->Extent().width);
}

static int screen_y() {
    auto& ctxt = RenderingContext::Get();
    return static_cast<int>(ctxt.Swapchain()->Extent().height);
}

static double z_near() {
    return 0.1;
}

static double z_far() {
    return 3000.0;
}

static double fov_y() {
    return glm::radians(75.0 * 0.50);
}

int main(int argc, char* argv[]) {

    namespace fs = std::experimental::filesystem;
    
    static const fs::path shader_tools_base_path{ fs::canonical("../../../../third_party/shadertools/fragments") };
    static const std::string base_path_str{ shader_tools_base_path.string() };
    static const fs::path shader_pack_path{ fs::canonical("../../../../third_party/shadertools/fragments/volumetric_forward/volumetric_forward.yaml") };
    static const std::string pack_path_str{ shader_pack_path.string() };
    static const fs::path saved_pack_bin_path{ fs::current_path() / "VolumetricForwardPack.stbin" };
    static const std::string saved_pack_bin_str{ saved_pack_bin_path.string() };

    void* storage_ptr = reinterpret_cast<void*>(&el::Helpers::storage());
    vpr::SetLoggingRepository_VprCore(storage_ptr);
    vpr::SetLoggingRepository_VprCommand(storage_ptr);
    vpr::SetLoggingRepository_VprResource(storage_ptr);
    vpr::SetLoggingRepository_VprAlloc(storage_ptr);
    
    auto& ctxt = RenderingContext::Get();
    ctxt.Construct("RendererContextCfg.json");

    ResourceContext& rsrc = ResourceContext::Get();
    rsrc.Initialize(ctxt.Device(), ctxt.PhysicalDevice());

    using namespace st;
    auto& callbacks = ShaderPack::RetrievalCallbacks();
    callbacks.GetScreenSizeX = screen_x;
    callbacks.GetScreenSizeY = screen_y;
    callbacks.GetFOVY = fov_y;
    callbacks.GetZNear = z_near;
    callbacks.GetZFar = z_far;

    std::unique_ptr<ShaderPack> vtfPack{ nullptr };
    ShaderGenerator::SetBasePath(base_path_str.c_str());

    if (!fs::exists(saved_pack_bin_path)) {
        vtfPack = std::make_unique<ShaderPack>(pack_path_str.c_str());
        ShaderPackBinary* bin_to_save = CreateShaderPackBinary(vtfPack.get());
        SaveBinaryToFile(bin_to_save, saved_pack_bin_str.c_str());
        DestroyShaderPackBinary(bin_to_save);
    }
    else {
        ShaderPackBinary* bin = LoadShaderPackBinary(saved_pack_bin_str.c_str());
        vtfPack = std::make_unique<ShaderPack>(bin);
        DestroyShaderPackBinary(bin);
        // should re-create after creating pack to ensure updates are propagated
        ShaderPackBinary* bin_to_save = CreateShaderPackBinary(vtfPack.get());
        SaveBinaryToFile(bin_to_save, saved_pack_bin_str.c_str());
        DestroyShaderPackBinary(bin_to_save);
    }

    auto& scene = VTF_Scene::Get();
    scene.Construct(RequiredVprObjects{ ctxt.Device(), ctxt.PhysicalDevice(), ctxt.Instance(), ctxt.Swapchain() }, vtfPack.get());

    while (!ctxt.ShouldWindowClose()) {
        ctxt.Update();
        scene.Render(nullptr);
    }

    return 0;
}