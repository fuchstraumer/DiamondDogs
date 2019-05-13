#pragma once
#ifndef DIAMOND_DOGS_PIPELINE_SUBMISSION_HPP
#define DIAMOND_DOGS_PIPELINE_SUBMISSION_HPP
#include "PipelineResource.hpp"
#include "delegate.hpp"
#include <memory>
#include <vector>
#include <string_view>
#include <variant>
#include <unordered_set>

namespace st {
    class ShaderGroup;
}

namespace vpr {
    class PipelineCache;
    class PipelineLayout;
}

class RenderGraph;

enum class ResourceUsageType : uint8_t
{
    None = 0u,
    DepthStencil,
    InputAttachment,
    HistoryInput,
    Color,
    ColorScale,
    Resolve,
    StorageTexture,
    Uniform,
    UniformTexelBuffer,
    StorageTexelBuffer,
    Storage,
    Texture,
    Blit,
    VertexBuffer,
    IndexBuffer,
    IndirectBuffer
};

enum class ResourceAccessType : uint8_t
{
    None = 0u,
    Read,
    Write,
    ReadWrite
};

struct ResourceUsageInfo
{
    std::string_view Name{ };
    std::string_view Input{ };
    ResourceUsageType Type{ ResourceUsageType::None };
    VkPipelineStageFlags PipelineStages{ VK_PIPELINE_STAGE_FLAG_BITS_MAX_ENUM };
    struct base_resource_t{};
    resource_info_variant_t Info{ generic_resource_info_t{} };
};

struct AccessedResource {
    VkPipelineStageFlags Stages{ 0 };
    VkAccessFlags Access{ 0 };
    VkImageLayout Layout{ VK_IMAGE_LAYOUT_UNDEFINED };
    PipelineResource* Info{ nullptr };
};

class PipelineSubmission {
    PipelineSubmission(const PipelineSubmission&) = delete;
    PipelineSubmission& operator=(const PipelineSubmission&) = delete;
    friend class RenderGraph;
    friend class FeatureRenderer;
public:

    PipelineSubmission(RenderGraph& graph, std::string name, size_t idx, uint32_t queue_idx);
    ~PipelineSubmission();

    void RecordCommands(VkCommandBuffer cmd);
    void AddShaders(const st::ShaderGroup* group);

    PipelineResource& AddResource(const ResourceUsageInfo& info, const ResourceAccessType access_type);

    PipelineResource& SetDepthStencilInput(const std::string& name);
    PipelineResource& SetDepthStencilOutput(const std::string& name, image_info_t info);
    PipelineResource& AddInputAttachment(const std::string& name);
    PipelineResource& AddHistoryInput(const std::string& name);
    PipelineResource& AddColorOutput(const std::string& name, image_info_t info, const std::string& input = "");
    PipelineResource& AddResolveOutput(const std::string& name, image_info_t info);
    PipelineResource& AddStorageTextureInput(const std::string& name, image_info_t info, const std::string& output = "");
    PipelineResource& AddStorageTextureOutput(const std::string& name, image_info_t info, const std::string& input = "");
    PipelineResource& AddStorageTextureRW(const std::string & name, image_info_t info);
    PipelineResource& AddUniformInput(const std::string& name, VkPipelineStageFlags stages = 0);
    PipelineResource& AddUniformTexelBufferInput(const std::string& name, VkPipelineStageFlags stages = 0);
    PipelineResource& AddInputTexelBufferReadOnly(const std::string& name, VkPipelineStageFlags stages = 0);
    PipelineResource& AddTexelBufferInput(const std::string& name, buffer_info_t info);
    PipelineResource& AddTexelBufferOutput(const std::string& name, buffer_info_t info, const std::string& input = "");
    PipelineResource& AddTexelBufferRW(const std::string& name, buffer_info_t info);
    PipelineResource& AddStorageOutput(const std::string& name, buffer_info_t info, const std::string& input = "");
    PipelineResource& AddStorageReadOnlyInput(const std::string& name, VkPipelineStageFlags stages = 0);
    PipelineResource& AddTextureInput(const std::string& name, VkPipelineStageFlags stages = 0);


    PipelineResource& AddVertexBufferInput(const std::string& name);
    PipelineResource& AddIndexBufferInput(const std::string& name);
    PipelineResource& AddIndirectBufferInput(const std::string& name);
    
    const std::vector<PipelineResource*>& GetColorOutputs() const noexcept;
    const std::vector<PipelineResource*>& GetResolveOutputs() const noexcept;
    const std::vector<PipelineResource*>& GetColorInputs() const noexcept;
    const std::vector<PipelineResource*>& GetColorScaleInputs() const noexcept;
    const std::vector<PipelineResource*>& GetStorageTextureInputs() const noexcept;
    const std::vector<PipelineResource*>& GetStorageTextureOutputs() const noexcept;
    const std::vector<PipelineResource*>& GetAttachmentInputs() const noexcept;
    const std::vector<PipelineResource*>& GetHistoryInputs() const noexcept;
    const std::vector<PipelineResource*>& GetUniformInputs() const noexcept;
    const std::vector<PipelineResource*>& GetStorageOutputs() const noexcept;
    const std::vector<PipelineResource*>& GetStorageInputs() const noexcept;
    const std::vector<PipelineResource*>& GetTexelBufferInputs() const noexcept;
    const std::vector<PipelineResource*>& GetTexelBufferOutputs() const noexcept;
    const std::vector<AccessedResource>& GetGenericTextureInputs() const noexcept;
    const std::vector<AccessedResource>& GetGenericBufferInputs() const noexcept;
    const PipelineResource* GetDepthStencilInput() const noexcept;
    const PipelineResource* GetDepthStencilOutput() const noexcept;
    const std::vector<std::string>& GetTags() const noexcept;

    void SetIdx(size_t idx);
    void SetPhysicalPassIdx(size_t idx);
    void SetName(std::string name);
    void AddTag(std::string _tag);
    void SetTags(std::vector<std::string> _tags);

    const size_t& GetIdx() const noexcept;
    const size_t& GetPhysicalPassIdx() const noexcept;
    const std::string& Name() const noexcept;

    bool NeedRenderPass() const noexcept;
    bool GetClearColor(size_t idx, VkClearColorValue* value = nullptr) noexcept;
    bool GetClearDepth(VkClearDepthStencilValue* value = nullptr) const noexcept;

    bool ValidateSubmission();
    void MakeColorInputScaled(const size_t& idx);
    
private:

    void validateColorInputs();

    PipelineResource& addGenericBufferInput(const std::string& name, VkPipelineStageFlags stages, VkAccessFlags access,
        VkBufferUsageFlags usage);

    void setDepthStencil(PipelineResource& rsrc, const ResourceAccessType access_type);
    void checkColorInput(const ResourceUsageInfo& info, PipelineResource& output_resource, const ResourceAccessType access_type);
    std::vector<PipelineResource*>& getSubtypeIters(const ResourceUsageType type, const ResourceAccessType access_type);

    struct resource_key
    {
        ResourceUsageType UsageType;
        ResourceAccessType AccessType;
    };

    struct resource_key_hash
    {
        constexpr size_t operator()(const resource_key& key) noexcept
        {
            // shift output 32 for first, or with shifted 32 of second
            return (std::hash<uint8_t>()(static_cast<uint8_t>(key.UsageType)) << 32) | (std::hash<uint8_t>()(static_cast<uint8_t>(key.AccessType)) >> 32);
        }
    };

    struct resource_key_equal
    {
        constexpr bool operator()(const resource_key& r0, const resource_key& r1) const noexcept
        {
            return (r0.UsageType == r1.UsageType) && (r0.AccessType == r1.AccessType);
        }
    };

    std::string name{};
    RenderGraph& graph;
    uint32_t queueIdx;
    size_t idx{ std::numeric_limits<size_t>::max() };
    size_t physicalPassIdx{ std::numeric_limits<size_t>::max() };

    delegate_t<void(VkCommandBuffer)> recordSubmissionCb;
    delegate_t<bool()> needPassCb;
    delegate_t<bool(size_t, VkClearColorValue*)> getClearColorValueCb;
    delegate_t<bool(VkClearDepthStencilValue*)> getClearDepthValueCb;

    std::unordered_map<resource_key, std::vector<PipelineResource*>, resource_key_hash, resource_key_equal> resources;
    // so we can access just the resources of a type
    std::unordered_map<resource_key, std::vector<PipelineResource*>> subtypeIterators;
    std::unordered_set<ResourceUsageType> usageTypeFlags;
    // normally hate hiding functions like this, sorry
    std::vector<AccessedResource> genericBuffers;
    std::vector<AccessedResource> genericTextures;
    PipelineResource* depthStencilInput{ nullptr };
    PipelineResource* depthStencilOutput{ nullptr };
    std::vector<std::string> usedSetNames;
    std::vector<std::string> submissionTags;

};

#endif //!DIAMOND_DOGS_PIPELINE_SUBMISSION_HPP
