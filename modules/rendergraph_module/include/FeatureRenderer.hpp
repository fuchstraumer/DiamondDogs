#pragma once
#ifndef VPSK_FEATURE_RENDERER_HPP
#define VPSK_FEATURE_RENDERER_HPP
#include "ForwardDecl.hpp"
#include <vulkan/vulkan.h>
#include <memory>
#include <unordered_map>
#include <variant>
#include <string>
#include <cstdint>

namespace st {
    class ShaderPack;
    class ShaderGroup;
}
namespace vpsk {

    /*
        Quick list of feature ideas:
        - Reflection
        - SSR
        - MotionBlur
        - Scene
        - WaterRipple
        - Depth (Linear/Logarithmic)
        - Particle
        - VolumetricLights (clustered forward stuff, too?)
        - Composition: might be best as a separate object, for interfacing 
            with the backbuffer and swapchain and such
        - DOF, Bloom, MotionBlur, Chomatic Aberration, SSAO, and so on... 
            - effectively, post processing runs. Maybe also good to as a separate class for FX/post-processing
              since these won't likely be using the same list of entities, instead applying shaders to fullscreen 
              quads and tris
            - would still need to integrate with the rendergraph, though
        - Debug visualizations
    */

    class RenderGraph;
    class RenderTarget;
    class PipelineSubmission;

    using feature_cvar_t = std::variant<
        float, double, uint32_t, int32_t, char, std::string, bool
    >;

    class FeatureRenderer {
        // RenderGraph should be able to freely play with this object.
        friend class RenderGraph;
    public:
        FeatureRenderer(RenderGraph& _graph, const char* lua_script_path);
        ~FeatureRenderer();

        feature_cvar_t& GetCVar(const std::string& name);
        const feature_cvar_t& GetCVar(const std::string& name) const;

        void AddEntity(std::uint32_t ent_handle);
        // Record commands into internal command buffer
        void operator()(VkCommandBuffer primary_cmd_buffer);

    private:
        st::ShaderPack* shaderPack;
        RenderGraph& graph;
        std::unique_ptr<RenderTarget> renderTarget;
        std::unique_ptr<vpr::CommandPool> commandPool;
        std::vector<PipelineSubmission*> submissions;
        std::unordered_map<std::string, feature_cvar_t> cvarMap;
    };

}

#endif //!VPSK_FEATURE_RENDERER_HPP
