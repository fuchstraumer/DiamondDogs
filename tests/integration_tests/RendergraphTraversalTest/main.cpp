#include "RenderingContext.hpp"
#include "ResourceContext.hpp"
#include "RenderGraph.hpp"
#include "core/Shader.hpp"
#include "core/ShaderPack.hpp"
#include <array>
#include <vector>
#include <unordered_map>
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
    
    ShaderPack pack("../fragments/volumetric_forward/ShaderPack.lua");

    RenderingContext& context = RenderingContext::Get();
    context.Construct("RendererContextCfg.json");
    
    RenderGraph& graph = RenderGraph::GetGlobalGraph();
    graph.AddShaderPack(&pack);

    std::cerr << "Tests complete.";
}
