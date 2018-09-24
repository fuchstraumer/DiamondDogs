#include "RenderingContext.hpp"
#include "ResourceContext.hpp"
#include "RenderGraph.hpp"
#include "PipelineSubmission.hpp"
#include "PipelineResource.hpp"
#include "objects/RenderTarget.hpp"
#include "objects/DepthTarget.hpp"
#include "core/Shader.hpp"
#include "core/ShaderPack.hpp"
#include "generation/ShaderGenerator.hpp"
#include <array>
#include <vector>
#include <unordered_map>
#include <chrono>
#include "easylogging++.h"
INITIALIZE_EASYLOGGINGPP

static int screen_x() {
    return 1920;
}

static int screen_y() {
    return 1080;
}

static double z_near() {
    return 0.1;
}

static double z_far() {
    return 3000.0;
}

static double fov_y() {
    return 75.0;
}


int main(int argc, char* argv[]) {
using namespace st; 

    auto& callbacks = ShaderPack::RetrievalCallbacks();
    
    callbacks.GetScreenSizeX = &screen_x;
    callbacks.GetScreenSizeY = &screen_y;
    callbacks.GetZNear = &z_near;
    callbacks.GetZFar = &z_far;
    callbacks.GetFOVY = &fov_y;

    std::chrono::high_resolution_clock::time_point timePointA, timePointB;

    ShaderGenerator::SetBasePath("../../../../third_party/shadertools/fragments/");
    timePointB = std::chrono::high_resolution_clock::now();
    ShaderPack pack("../../../../third_party/shadertools/fragments/volumetric_forward/ShaderPack.lua");
    timePointA = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> work_time = timePointA - timePointB;
    LOG(INFO) << "ShaderPack construction took " << work_time.count() << "ms.";

    RenderingContext& context = RenderingContext::Get();
    context.Construct("RendererContextCfg.json");

    ResourceContext& rsrc = ResourceContext::Get();
    rsrc.Construct(context.Device(), context.PhysicalDevice());
    
    RenderGraph& graph = RenderGraph::GetGlobalGraph();

    auto depth_pre_pass_fn = [](PipelineSubmission& pass) {
        RenderGraph& rg = RenderGraph::GetGlobalGraph();
        pass.SetDepthStencilOutput("backbuffer_depth", rg.GetBackbuffer()->Depth()->GetImageInfo());
    };
    
    auto depth_pass_read_depth = [](PipelineSubmission& pass) {
        RenderGraph& rg = RenderGraph::GetGlobalGraph();
        pass.SetDepthStencilInput("backbuffer_depth");
    };

    graph.AddTagFunction("DepthOnly", delegate_t<void(PipelineSubmission&)>::create(depth_pre_pass_fn));
    graph.AddTagFunction("DepthOnlyAsInput", delegate_t<void(PipelineSubmission&)>::create(depth_pass_read_depth));
    graph.AddShaderPack(&pack);
    graph.Bake();
    graph.Reset();

    rsrc.Destroy();
    std::cerr << "Tests complete.";
}
