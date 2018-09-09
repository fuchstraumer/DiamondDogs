#pragma once
#ifndef VPSK_PIPELINE_SUBMISSION_HPP
#define VPSK_PIPELINE_SUBMISSION_HPP
#include "PipelineResource.hpp"
#include <memory>
#include <vector>
#include "chrysocyon/signal/Delegate.hpp"

namespace st {
    class ShaderGroup;
}


class RenderGraph;

class PipelineSubmission {
    PipelineSubmission(const PipelineSubmission&) = delete;
    PipelineSubmission& operator=(const PipelineSubmission&) = delete;
    friend class RenderGraph;
    friend class FeatureRenderer;
public:

    PipelineSubmission(RenderGraph& graph, std::string name, size_t idx, VkPipelineStageFlags stages);
    ~PipelineSubmission();

    void RecordCommands(VkCommandBuffer cmd);
    void AddShaders(const st::ShaderGroup* group);

    PipelineResource& SetDepthStencilInput(const std::string& name);
    PipelineResource& SetDepthStencilOutput(const std::string& name, image_info_t info);
    PipelineResource& AddInputAttachment(const std::string& name);
    PipelineResource& AddHistoryInput(const std::string& name);
    PipelineResource& AddColorOutput(const std::string& name, image_info_t info, const std::string& input = "");
    PipelineResource& AddResolveOutput(const std::string& name, image_info_t info);
    PipelineResource& AddTextureInput(const std::string& name, image_info_t info);
    PipelineResource& AddStorageTextureInput(const std::string& name, image_info_t info, const std::string& output = "");
    PipelineResource& AddStorageTextureOutput(const std::string& name, image_info_t info, const std::string& input = "");
    PipelineResource& AddStorageTextureRW(const std::string & name, image_info_t info);
    PipelineResource& AddUniformInput(const std::string& name);
    PipelineResource& AddStorageOutput(const std::string& name, buffer_info_t info, const std::string& input = "");
    PipelineResource& AddStorageRW(const std::string& name, buffer_info_t info);
    PipelineResource& AddStorageReadOnlyInput(const std::string& name);

    const std::vector<PipelineResource*>& GetColorOutputs() const noexcept;
    const std::vector<PipelineResource*>& GetResolveOutputs() const noexcept;
    const std::vector<PipelineResource*>& GetColorInputs() const noexcept;
    const std::vector<PipelineResource*>& GetColorScaleInputs() const noexcept;
    const std::vector<PipelineResource*>& GetTextureInputs() const noexcept;
    const std::vector<PipelineResource*>& GetStorageTextureInputs() const noexcept;
    const std::vector<PipelineResource*>& GetStorageTextureOutputs() const noexcept;
    const std::vector<PipelineResource*>& GetAttachmentInputs() const noexcept;
    const std::vector<PipelineResource*>& GetHistoryInputs() const noexcept;
    const std::vector<PipelineResource*>& GetUniformInputs() const noexcept;
    const std::vector<PipelineResource*>& GetStorageOutputs() const noexcept;
    const std::vector<PipelineResource*>& GetStorageReadOnlyInputs() const noexcept;
    const std::vector<PipelineResource*>& GetStorageInputs() const noexcept;
    const std::vector<std::string>& GetTags() const noexcept;
    const PipelineResource* GetDepthStencilInput() const noexcept;
    const PipelineResource* GetDepthStencilOutput() const noexcept;

    void SetIdx(size_t idx);
    void SetPhysicalPassIdx(size_t idx);
    void SetStages(VkPipelineStageFlags _stages);
    void SetName(std::string name);
    void AddTag(std::string _tag);
    void SetTags(std::vector<std::string> _tags);

    const size_t& GetIdx() const noexcept;
    const size_t& GetPhysicalPassIdx() const noexcept;
    const VkPipelineStageFlags& GetStages() const noexcept;
    const std::string& Name() const noexcept;

    bool NeedRenderPass() const noexcept;
    bool GetClearColor(size_t idx, VkClearColorValue* value = nullptr) noexcept;
    bool GetClearDepth(VkClearDepthStencilValue* value = nullptr) const noexcept;

    bool ValidateSubmission();
    void MakeColorInputScaled(const size_t& idx);

private:

    void traverseDependencies(size_t& stack_count);
    enum traversal_flags : uint32_t {
        NoCheck = 0x00000001,
        IgnoreSelf = 0x00000002,
        MergeDependency = 0x00000004
    };
    void dependencyTraversalRecursion(const std::unordered_set<size_t>& passes, size_t& stack_count, const uint32_t& flags);

    std::string name{};
    RenderGraph& graph;
    size_t idx{ std::numeric_limits<size_t>::max() };
    size_t physicalPassIdx{ std::numeric_limits<size_t>::max() };
    VkPipelineStageFlags stages{ VK_PIPELINE_STAGE_FLAG_BITS_MAX_ENUM };

    delegate_t<void(VkCommandBuffer)> recordSubmissionCb;
    delegate_t<bool()> needPassCb;
    delegate_t<bool(size_t, VkClearColorValue*)> getClearColorValueCb;
    delegate_t<bool(VkClearDepthStencilValue*)> getClearDepthValueCb;

    std::vector<PipelineResource*> colorOutputs;
    std::vector<PipelineResource*> resolveOutputs;
    std::vector<PipelineResource*> colorInputs;
    std::vector<PipelineResource*> colorScaleInputs;
    std::vector<PipelineResource*> textureInputs;
    std::vector<PipelineResource*> storageTextureInputs;
    std::vector<PipelineResource*> storageTextureOutputs;
    std::vector<PipelineResource*> attachmentInputs;
    std::vector<PipelineResource*> historyInputs;
    std::vector<PipelineResource*> uniformInputs;
    std::vector<PipelineResource*> storageOutputs;
    std::vector<PipelineResource*> storageReadOnlyInputs;
    std::vector<PipelineResource*> storageInputs;

    PipelineResource* depthStencilInput{ nullptr };
    PipelineResource* depthStencilOutput{ nullptr };

    std::vector<std::string> usedSetNames;
    std::vector<std::string> submissionTags;
    std::unique_ptr<vpr::PipelineCache> cache;
    std::unique_ptr<vpr::PipelineLayout> layout;
    VkPipeline pipelineHandle = VK_NULL_HANDLE;

};

#endif //!VPSK_PIPELINE_SUBMISSION_HPP
