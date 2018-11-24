#include "VTF_Scene.hpp"
#include "core/ShaderPack.hpp"
#include "RenderingContext.hpp"
#include "ResourceContext.hpp"
#include "Swapchain.hpp"
#include "core/ShaderPack.hpp"
#include "generation/ShaderGenerator.hpp"
#include "easylogging++.h"
INITIALIZE_EASYLOGGINGPP
#include "glm/gtc/matrix_transform.hpp"

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

    auto& ctxt = RenderingContext::Get();
    ctxt.Construct("RendererContextCfg.json");

    ResourceContext& rsrc = ResourceContext::Get();
    rsrc.Construct(ctxt.Device(), ctxt.PhysicalDevice());

    using namespace st;
    auto& callbacks = ShaderPack::RetrievalCallbacks();
    callbacks.GetScreenSizeX = screen_x;
    callbacks.GetScreenSizeY = screen_y;
    callbacks.GetFOVY = fov_y;
    callbacks.GetZNear = z_near;
    callbacks.GetZFar = z_far;

    ShaderGenerator::SetBasePath("../../../../third_party/shadertools/fragments");
    ShaderPack vtfPack("../../../../third_party/shadertools/fragments/volumetric_forward/Shaderpack.lua");

    auto& scene = VTF_Scene::Get();
    scene.Construct(RequiredVprObjects{ ctxt.Device(), ctxt.PhysicalDevice(), ctxt.Instance(), ctxt.Swapchain() }, &vtfPack);

    while (!ctxt.ShouldWindowClose()) {
        ctxt.Update();
        scene.Render(nullptr);
    }

    return 0;
}