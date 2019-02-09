#pragma once
#ifndef DIAMOND_DOGS_RENDERGRAPH_HPP
#define DIAMOND_DOGS_RENDEGRAPH_HPP
#include <vulkan/vulkan.h>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <stack>
#include <variant>
#include "core/ResourceUsage.hpp"
#include "PipelineResource.hpp"
#include "delegate.hpp"
#include "tagged_bool.hpp"

namespace st {
    class ShaderPack;
    class ShaderResource;
    class Shader;
}

namespace vpr {
    class Device;
}

class PipelineSubmission;
class DescriptorPack;
class RenderTarget;

class RenderGraph {
    friend class PipelineSubmission;
public:

    RenderGraph(const vpr::Device* dvc);
    ~RenderGraph();

    void AddShaderPack(const st::ShaderPack* pack);

    PipelineSubmission& AddPipelineSubmission(const std::string& name, VkPipelineStageFlags stages);
    PipelineResource& GetResource(const std::string& name);
    const DescriptorPack* GetPackResources(const std::string& name) const;
    void Bake();
    void Reset();

    void AddTagFunction(const std::string& tag, delegate_t<void(PipelineSubmission&)> fn);

    void SetBackbufferSource(const std::string& name);
    void SetBackbufferDimensions(const resource_dimensions_t& dimensions);
    resource_dimensions_t GetResourceDimensions(const PipelineResource& rsrc);
    size_t NumSubmissions() const noexcept;
    const RenderTarget* GetBackbuffer() const noexcept;
    const vpr::Device* GetDevice() const noexcept;

    static RenderGraph& GetGlobalGraph();
    
private:
    using NoCheck = tagged_bool<struct NoCheck_tag>;
    using IgnoreSelf = tagged_bool<struct IgnoreSelf_tag>;
    using MergeDependency = tagged_bool<struct MergeDependency_tag>;

    void traverseDependencies(const PipelineSubmission& submission, size_t stack_count);
    void dependencyTraversalRecursion(const PipelineSubmission& submission, const std::unordered_set<size_t>& passes, size_t stack_count, const NoCheck no_check,
        const IgnoreSelf ignore_self, const MergeDependency merge_dependency);

    void addShaderPackResources(const st::ShaderPack* pack);
    buffer_info_t createPipelineResourceBufferInfo(const st::ShaderResource* resource) const;
    image_info_t createPipelineResourceImageInfo(const st::ShaderResource* resource) const;
    void createPipelineResourcesFromPack(const st::ShaderPack* pack);
    void addResourceUsagesToSubmission(PipelineSubmission& submissions, const std::vector<st::ResourceUsage>& usages);
    void addSingleGroup(const std::string& name, const st::Shader* group);
    void addSubmissionsFromPack(const st::ShaderPack* pack);

    std::vector<const st::ShaderPack*> shaderPacks;
    std::unordered_map<std::string, std::unique_ptr<DescriptorPack>> packResources;

    struct pipeline_barrier_t {
        size_t Resource;
        VkImageLayout Layout{ VK_IMAGE_LAYOUT_UNDEFINED };
        VkAccessFlags AccessFlags{ VK_ACCESS_FLAG_BITS_MAX_ENUM };
        VkPipelineStageFlags Stages{ VK_PIPELINE_STAGE_FLAG_BITS_MAX_ENUM };
        bool History{ false };
    };

    struct pass_barriers_t {
        std::vector<pipeline_barrier_t> InvalidateBarriers;
        std::vector<pipeline_barrier_t> FlushBarriers;
    };

    struct color_clear_request_t {
        PipelineSubmission* Submission;
        VkClearColorValue* Target;
        size_t Index;
    };

    struct depth_clear_request_t {
        PipelineSubmission* Submission;
        VkClearDepthStencilValue* Target;
    };

    struct scaled_clear_request_t {
        size_t Target;
        size_t Resource;
    };

    struct mipmap_request_t {
        size_t Resource;
    };

    std::string graphName;
    std::string backbufferSource{ "backbuffer" };
    std::unordered_map<std::string, pass_barriers_t> passBarriers;
    std::unordered_map<std::string, size_t> submissionNameMap;
    std::vector<std::unique_ptr<PipelineSubmission>> submissions;
    std::unordered_map<std::string, size_t> resourceNameMap;
    std::unordered_map<std::string, std::unique_ptr<RenderTarget>> renderTargets;
    std::vector<std::unique_ptr<PipelineResource>> pipelineResources; 
    std::unordered_map<std::string, delegate_t<void(PipelineSubmission&)>> tagFunctionsMap;
    const vpr::Device* device;

    std::vector<std::unordered_set<size_t>> submissionDependencies;
    std::vector<std::unordered_set<size_t>> submissionMergeDependencies;
    std::vector<size_t> submissionStack;
};

#endif // !DIAMOND_DOGS_RENDERGRAPH_HPP
