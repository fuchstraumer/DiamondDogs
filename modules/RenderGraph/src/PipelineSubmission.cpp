#include "PipelineSubmission.hpp"
#include "RenderGraph.hpp"
#include "easylogging++.h"
#include "PipelineLayout.hpp"
#include "PipelineCache.hpp"
#include "LogicalDevice.hpp"
#include "RenderingContext.hpp"
#include "easylogging++.h"
#include <vector>
#include <cassert>

PipelineSubmission::PipelineSubmission(RenderGraph& rgraph, std::string _name, size_t _idx, uint32_t _queue)
    : graph(rgraph), name(std::move(_name)), idx(std::move(_idx)), queueIdx(std::move(_queue)) {}

PipelineSubmission::~PipelineSubmission() {}

PipelineResource& PipelineSubmission::AddResource(const ResourceUsageInfo& info, const ResourceAccessType access_type)
{

    auto& resource = graph.GetResource(info.Name);
    //resources.emplace(resource_key{ info.Type, access_type }, &resource);

    resource.AddQueue(idx, queueIdx);

    switch (access_type)
    {
    case ResourceAccessType::Read:
        resource.SetReadBySubmission(idx);
        break;
    case ResourceAccessType::Write:
        resource.SetWrittenBySubmission(idx);
        break;
    case ResourceAccessType::ReadWrite:
        resource.SetReadBySubmission(idx);
        resource.SetWrittenBySubmission(idx);
        break;
    default:
        LOG(ERROR) << "Invalid access_type enum value!";
        throw std::domain_error("Invalid access_type enum value!");
    };

    switch (info.Type)
    {
    case ResourceUsageType::DepthStencil:
        resource.AddImageUsage(idx, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
        setDepthStencil(resource, access_type);
        break;
    case ResourceUsageType::InputAttachment:
        resource.AddImageUsage(idx, VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT);
        break;
    case ResourceUsageType::HistoryInput:
        break;
    case ResourceUsageType::Color:
        resource.AddImageUsage(idx, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
        resource.SetInfo(info.Info);
        checkColorInput(info, resource, access_type);
        break;
    case ResourceUsageType::Resolve:
        resource.AddImageUsage(idx, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
        resource.SetInfo(info.Info);
        break;
    case ResourceUsageType::StorageTexture:
        break;
    case ResourceUsageType::Uniform:
        break;
    case ResourceUsageType::UniformTexelBuffer:
        break;
    case ResourceUsageType::StorageTexelBuffer:
        break;
    case ResourceUsageType::Storage:
        break;
    case ResourceUsageType::Texture:
        break;
    case ResourceUsageType::Blit:
        break;
    case ResourceUsageType::VertexBuffer:
        break;
    case ResourceUsageType::IndexBuffer:
        break;
    case ResourceUsageType::IndirectBuffer:
        break;
    default:
        LOG(ERROR) << "Invalid resource usage enum value in ResourceUsageInfo struct!";
        throw std::domain_error("Invalid resource usage enum value!");
    };
}

/*
PipelineResource& PipelineSubmission::AddColorOutput(const std::string& name, image_info_t info, const std::string& input)
{
    auto& resource = graph.GetResource(name);
    resource.AddQueue(idx, queueIdx);
    resource.SetWrittenBySubmission(idx);
    resource.SetInfo(info);
    resource.AddImageUsage(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
    colorOutputs.emplace_back(&resource);

    if (!input.empty())
    {
        auto& input_resource = graph.GetResource(input);
        input_resource.SetReadBySubmission(idx);
        input_resource.AddImageUsage(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
        colorInputs.emplace_back(&input_resource);
        colorScaleInputs.emplace_back(&input_resource);
    }
    else
    {
        colorInputs.emplace_back(nullptr);
        colorScaleInputs.emplace_back(nullptr);
    }

    return resource;
}

PipelineResource& PipelineSubmission::AddResolveOutput(const std::string& name, image_info_t info)
{
    auto& resource = graph.GetResource(name);
    resource.AddQueue(idx, queueIdx);
    resource.SetWrittenBySubmission(idx);        
    if (info.Samples != VK_SAMPLE_COUNT_1_BIT)
    {
        LOG(ERROR) << "Image info for resolve output attachment has invalid sample count flags value!";
        throw std::runtime_error("Invalid sample count flags.");
    }
    resource.SetInfo(info);
    resource.AddImageUsage(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
    resolveOutputs.emplace_back(&resource);
    return resource;
}

PipelineResource& PipelineSubmission::AddStorageTextureInput(const std::string & name, image_info_t info, const std::string& output)
{
    auto& resource = graph.GetResource(name);
    resource.AddQueue(idx, queueIdx);
    resource.SetWrittenBySubmission(idx);
    resource.SetInfo(info);
    resource.AddImageUsage(VK_IMAGE_USAGE_STORAGE_BIT);
    storageTextureInputs.emplace_back(&resource);
    if (!output.empty())
    {
        auto& output_resource = graph.GetResource(output);
        output_resource.SetWrittenBySubmission(idx);
        output_resource.AddImageUsage(VK_IMAGE_USAGE_STORAGE_BIT);
        storageTextureOutputs.emplace_back(&output_resource);
    }
    else
    {
        storageTextureOutputs.emplace_back(nullptr);
    }
    return resource;
}

PipelineResource& PipelineSubmission::AddStorageTextureOutput(const std::string& name, image_info_t info, const std::string& input)
{
    auto& resource = graph.GetResource(name);
    resource.AddQueue(idx, queueIdx);
    resource.SetWrittenBySubmission(idx);
    resource.SetInfo(info);
    resource.AddImageUsage(VK_IMAGE_USAGE_STORAGE_BIT);
    storageTextureOutputs.emplace_back(&resource);

    if (!input.empty())
    {
        auto& input_resource = graph.GetResource(input);
        input_resource.SetReadBySubmission(idx);
        input_resource.AddImageUsage(VK_IMAGE_USAGE_STORAGE_BIT);
        storageTextureInputs.emplace_back(&input_resource);
    }
    else
    {
        storageTextureInputs.emplace_back(nullptr);
    }

    return resource;
}

PipelineResource& PipelineSubmission::AddStorageTextureRW(const std::string& name, image_info_t info)
{
    auto& resource = graph.GetResource(name);
    resource.AddQueue(idx, queueIdx);
    resource.AddImageUsage(VK_IMAGE_USAGE_STORAGE_BIT);
    resource.SetReadBySubmission(idx);
    resource.SetWrittenBySubmission(idx);
    storageTextureOutputs.emplace_back(&resource);
    storageTextureInputs.emplace_back(&resource);
    return resource;
}
*/

PipelineResource& PipelineSubmission::AddUniformInput(const std::string& _name, VkPipelineStageFlags stages)
{
    auto& resource = graph.GetResource(_name);
    auto& queue_family_indices = RenderingContext::Get().Device()->QueueFamilyIndices();
    
    if (stages == 0)
    {
        if (queueIdx == queue_family_indices.Compute)
        {
            stages = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        }
        else
        {
            stages = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        }
    }

    return addGenericBufferInput(name, stages, VK_ACCESS_SHADER_READ_BIT, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
}

PipelineResource& PipelineSubmission::AddUniformTexelBufferInput(const std::string& name, VkPipelineStageFlags stages)
{
    auto& resource = graph.GetResource(name);
    auto& queue_family_indices = RenderingContext::Get().Device()->QueueFamilyIndices();

    if (stages == 0)
    {
        if (queueIdx == queue_family_indices.Compute)
        {
            stages |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        }
        else
        {
            stages |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        }
    }

    return addGenericBufferInput(name, stages, VK_ACCESS_SHADER_READ_BIT, VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT);
}

PipelineResource& PipelineSubmission::AddInputTexelBufferReadOnly(const std::string& name, VkPipelineStageFlags stages)
{
    auto& resource = graph.GetResource(name);
    auto& queue_family_indices = RenderingContext::Get().Device()->QueueFamilyIndices();

    if (stages == 0)
    {
        if (queueIdx == queue_family_indices.Compute)
        {
            stages = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        }
        else
        {
            stages = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        }
    }

    return addGenericBufferInput(name, stages, VK_ACCESS_SHADER_READ_BIT, VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT);
}

PipelineResource& PipelineSubmission::AddTexelBufferInput(const std::string& name, buffer_info_t info)
{
    auto& resource = graph.GetResource(name);

    resource.SetInfo(info);
    //resource.AddBufferUsage(VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT);
    resource.SetReadBySubmission(idx);
    resource.AddQueue(idx, queueIdx);

    return resource;
}

//PipelineResource& PipelineSubmission::AddTexelBufferOutput(const std::string& name, buffer_info_t info, const std::string& input)
//{
//    auto& resource = graph.GetResource(name);
//    resource.SetInfo(info);
//    resource.AddBufferUsage(VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT);
//    resource.SetWrittenBySubmission(idx);
//    resource.AddQueue(idx, queueIdx);
//    texelBufferOutputs.emplace_back(&resource);
//
//    if (!input.empty())
//    {
//        auto& input_rsrc = graph.GetResource(input);
//        input_rsrc.SetReadBySubmission(idx);
//        input_rsrc.AddBufferUsage(VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT);
//        texelBufferInputs.emplace_back(&input_rsrc);
//    }
//    else
//    {
//        texelBufferInputs.emplace_back(nullptr);
//    }
//
//    return resource;
//}
//
//PipelineResource& PipelineSubmission::AddTexelBufferRW(const std::string & name, buffer_info_t info)
//{
//    auto& resource = graph.GetResource(name);
//    resource.SetInfo(info);
//    resource.AddBufferUsage(VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT);
//    resource.SetWrittenBySubmission(idx);
//    resource.SetReadBySubmission(idx);
//    resource.AddQueue(idx, queueIdx);
//    texelBufferInputs.emplace_back(&resource);
//    texelBufferOutputs.emplace_back(&resource);
//    return resource;
//}
//
//PipelineResource& PipelineSubmission::AddStorageOutput(const std::string& name, buffer_info_t info, const std::string& input)
//{
//    auto& resource = graph.GetResource(name);
//    resource.AddQueue(idx, queueIdx);
//    resource.SetWrittenBySubmission(idx);
//    resource.SetInfo(info);
//    storageOutputs.emplace_back(&resource);
//    if (!input.empty())
//    {
//        auto& input_resource = graph.GetResource(input);
//        input_resource.SetReadBySubmission(idx);
//        storageInputs.emplace_back(&input_resource);
//    }
//    else
//    {
//        storageInputs.emplace_back(nullptr);
//    }
//    return resource;
//}

PipelineResource& PipelineSubmission::AddStorageReadOnlyInput(const std::string& name, VkPipelineStageFlags stages) {
    auto& queue_family_indices = RenderingContext::Get().Device()->QueueFamilyIndices();

    if (stages == 0) {
        if (queueIdx == queue_family_indices.Compute) {
            stages = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        }
        else {
            stages = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        }
    }
    
    return addGenericBufferInput(name, stages, VK_ACCESS_SHADER_READ_BIT, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
}

PipelineResource& PipelineSubmission::AddTextureInput(const std::string& name, VkPipelineStageFlags stages) {
    auto& queue_family_indices = RenderingContext::Get().Device()->QueueFamilyIndices();

    auto& resource = graph.GetResource(name);
    resource.AddQueue(idx, queueIdx);
    resource.SetReadBySubmission(idx);

    AccessedResource acc;
    acc.Info = &resource;
    acc.Layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    acc.Access = VK_ACCESS_SHADER_READ_BIT;

    if (stages != 0)
    {
        acc.Stages = stages;
    }
    else if (queueIdx == queue_family_indices.Compute)
    {
        acc.Stages = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    }
    else
    {
        acc.Stages = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }

    genericTextures.emplace_back(acc);
    return resource;
}

const std::vector<std::string>& PipelineSubmission::GetTags() const noexcept
{
    return submissionTags;
}

const PipelineResource* PipelineSubmission::GetDepthStencilInput() const noexcept
{
    return depthStencilInput;
}

const PipelineResource* PipelineSubmission::GetDepthStencilOutput() const noexcept
{
    return depthStencilOutput;
}

void PipelineSubmission::SetIdx(size_t _idx)
{
    idx = std::move(_idx);
}

void PipelineSubmission::SetPhysicalPassIdx(size_t idx)
{
    physicalPassIdx = std::move(idx);
}

void PipelineSubmission::SetName(std::string _name)
{
    name = std::move(_name);
}

void PipelineSubmission::AddTag(std::string _tag)
{
    submissionTags.emplace_back(std::move(_tag));
}

void PipelineSubmission::SetTags(std::vector<std::string> _tags)
{
    submissionTags = std::move(_tags);
}

const size_t& PipelineSubmission::GetIdx() const noexcept
{
    return idx;
}

const size_t& PipelineSubmission::GetPhysicalPassIdx() const noexcept
{
    return physicalPassIdx;
}

const std::string& PipelineSubmission::Name() const noexcept
{
    return name;
}

bool PipelineSubmission::NeedRenderPass() const noexcept
{
    if (!needPassCb)
    {
        return true;
    }
    else
    {
        return needPassCb();
    }
}

bool PipelineSubmission::GetClearColor(size_t idx, VkClearColorValue* value) noexcept
{
    if (!getClearColorValueCb)
    {
        return false;
    }
    else
    {
        return getClearColorValueCb(idx, value);
    }
}

bool PipelineSubmission::GetClearDepth(VkClearDepthStencilValue* value) const noexcept
{
    if (!getClearDepthValueCb)
    {
        return false;
    }
    else
    {
        return getClearDepthValueCb(value);
    }
}

void attachmentErr(const std::string& attachment_type, const std::string& submission_name)
{
    LOG(ERROR) << "Pipeline submission " << submission_name << " has a mismatched quantity of " << attachment_type << " inputs and outputs!";
    throw std::logic_error(std::string("Invalid " + attachment_type + " counts"));
}

bool PipelineSubmission::ValidateSubmission()
{

    /*if (storageInputs.size() != storageOutputs.size())
    {
        attachment_count_err("storage buffer");
    }

    if (storageTextureInputs.size() != storageTextureOutputs.size())
    {
        attachment_count_err("storage texture");
    }

    if (texelBufferInputs.size() != texelBufferOutputs.size())
    {
        attachment_count_err("storage texel buffer");
    }

    if (!resolveOutputs.empty())
    {
        if (resolveOutputs.size() != colorOutputs.size())
        {
            attachment_count_err("resolve attachment");
        }
    }

    if (!storageOutputs.empty())
    {
        for (size_t i = 0; i < storageOutputs.size(); ++i)
        {
            if (storageInputs[i] == nullptr) {
                continue;
            }

            if (storageOutputs[i]->GetInfo() != storageInputs[i]->GetInfo()) {
                LOG(ERROR) << "Mismatch between paired storage input-outputs, when comparing their buffer info objects!";
                throw std::logic_error("Mismatched storage buffer input-output info!");
            }
        }
    }

    if (!storageTextureOutputs.empty()) {
        for (size_t i = 0; i < storageTextureOutputs.size(); ++i) {
            if (storageTextureInputs[i] == nullptr) {
                continue;
            }

            if (storageTextureOutputs[i]->GetInfo() != storageTextureInputs[i]->GetInfo()) {
                LOG(ERROR) << "Mismatch between storage texture input-outputs, when comparing their image info objects!";
                throw std::logic_error("Mismatched storage texture input-output infos!");
            }
        }
    }

    if (!texelBufferOutputs.empty()) {
        for (size_t i = 0; i < texelBufferOutputs.size(); ++i) {
            if (texelBufferInputs[i] == nullptr) {
                continue;
            }

            if (texelBufferOutputs[i]->GetInfo() != texelBufferInputs[i]->GetInfo()) {
                LOG(ERROR) << "Mismatch between storage texel buffer input-outputs, incompatible dimensions despite R-M-W!";
                throw std::logic_error("Mismatched storage texel buffer input-output dimensions, incompatability detected!");
            }
        }
    }

    if ((depthStencilInput != nullptr) && (depthStencilOutput != nullptr)) {
        if (depthStencilInput->GetInfo() != depthStencilOutput->GetInfo()) {
            LOG(ERROR) << "Depth stencil input and output are mismatched in submission " << name << "!";
            throw std::logic_error("Mismatched depth stencil input-output!");
        }
    }*/

    return true;
}

void PipelineSubmission::validateColorInputs()
{
    if (usageTypeFlags.count(ResourceUsageType::Color) == 0)
    {
        return;
    }

    auto& colorInputs = getSubtypeIters(ResourceUsageType::Color, ResourceAccessType::Read);
    auto& colorOutputs = getSubtypeIters(ResourceUsageType::Color, ResourceAccessType::Write);
    auto& colorScaleInputs = getSubtypeIters(ResourceUsageType::ColorScale, ResourceAccessType::Read);

    if (colorInputs.size() != colorOutputs.size())
    {
        attachmentErr("color attachment", name);
    }

    for (size_t i = 0; i < colorInputs.size(); ++i)
    {
        if (colorInputs[i] == nullptr)
        {
            continue;
        }

        if (graph.GetResourceDimensions(*colorInputs[i]) != graph.GetResourceDimensions(*colorOutputs[i])) {
            // Need to scale/modify color input before it becomes the output
            assert(!colorScaleInputs.empty());
            std::swap(colorInputs[i], colorScaleInputs[i]);
        }

    }

}

PipelineResource& PipelineSubmission::addGenericBufferInput(const std::string& name, VkPipelineStageFlags stages, VkAccessFlags access, VkBufferUsageFlags usage) {
    auto& resource = graph.GetResource(name);
    resource.AddQueue(idx, queueIdx);
    resource.SetReadBySubmission(idx);
    //resource.AddBufferUsage(usage);

    AccessedResource acc;
    acc.Info = &resource;
    acc.Layout = VK_IMAGE_LAYOUT_GENERAL;
    acc.Access = access;
    acc.Stages = stages;
    genericBuffers.emplace_back(acc);

    return resource;
}

void PipelineSubmission::setDepthStencil(PipelineResource& rsrc, const ResourceAccessType access_type)
{
    switch (access_type)
    {
    case ResourceAccessType::Read:
        depthStencilInput = &rsrc;
        break;
    case ResourceAccessType::Write:
        depthStencilOutput = &rsrc;
        break;
    default:
        LOG(ERROR) << "Invalid access type for a depth stencil resource!";
        throw std::domain_error("Invalid Depth-Stencil access type!");
    };
}

void PipelineSubmission::checkColorInput(const ResourceUsageInfo& info, PipelineResource& output_resource, const ResourceAccessType access_type)
{
    if (!info.Input.empty())
    {

        if ((info.Input == info.Name) && (access_type != ResourceAccessType::ReadWrite))
        {
            // Our resource is read-write and the system isn't aware of this. This is an error and will cause issues.
            LOG(ERROR) << "Resource not specified as read-write is being used as a color input AND output!";
            throw std::runtime_error("Resource unspecified as read-write being used as such!");
        }

        auto& input_resource = graph.GetResource(info.Input);
        input_resource.AddImageUsage(idx, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
        input_resource.SetReadBySubmission(idx);
        
        //auto iter = resources.emplace(resource_key{ info.Type, access_type }, &input_resource);
        /*if (!iter.second)
        {
            LOG(ERROR) << "Failed to add resource to internal containers!";
        }*/

    }
}

std::vector<PipelineResource*>& PipelineSubmission::getSubtypeIters(const ResourceUsageType type, const ResourceAccessType access_type)
{
    const resource_key desiredKey{ type, access_type };

    if (subtypeIterators.count(desiredKey) == 0)
    {
        // generate the vector.
        auto& newVector = subtypeIterators[desiredKey];

        for (auto iter = resources.begin(); iter != resources.end(); ++iter)
        {
            if (iter->first.UsageType == type)
            {
                //newVector.emplace_back(iter->second);
            }
        }

        return newVector;
    }
    else
    {
        return subtypeIterators.at(desiredKey);
    }
}

void PipelineSubmission::RecordCommands(VkCommandBuffer cmd) {
    recordSubmissionCb(cmd);
}

#ifdef DIAMOND_DOGS_TESTING_ENABLED
TEST_SUITE("PipelineSubmission") {
    TEST_CASE("DepthStencilAttachments") {
        using namespace vpsk;
        PipelineSubmission submission(RenderGraph::GetGlobalGraph(), "TestDepthSubmission", 0, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT);
        auto& input = submission.SetDepthStencilInput("DepthStencilInput");
        auto* input_retrieved = submission.GetDepthStencilInput();
        CHECK(input == *input_retrieved);
        image_info_t info;
        auto& output = submission.SetDepthStencilOutput("DepthStencilOutput", info);
        auto* output_retrieved = submission.GetDepthStencilOutput();
        CHECK(output == *output_retrieved);
    }
    TEST_CASE("InputAttachments") {
        using namespace vpsk;
        PipelineSubmission submission(RenderGraph::GetGlobalGraph(), "TestInputAttachments", 0, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
        auto& resource = submission.AddInputAttachment("TestInput");
        auto& inputs = submission.GetAttachmentInputs();
        CHECK(std::any_of(inputs.cbegin(), inputs.cend(), [resource](const PipelineResource* rsrc) { return *rsrc == resource; }));
    }
    TEST_CASE("HistoryInputs") {
        using namespace vpsk;
        PipelineSubmission submission(RenderGraph::GetGlobalGraph(), "TestHistoryInputs", 0, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
        auto& resource = submission.AddHistoryInput("TestHistoryInput");
        auto& inputs = submission.GetHistoryInputs();
        CHECK(std::any_of(inputs.cbegin(), inputs.cend(), [resource](const PipelineResource* rsrc) { return *rsrc == resource; }));
    }
    TEST_CASE("SingleColorOutputNoInput") {
        using namespace vpsk;
        PipelineSubmission submission(RenderGraph::GetGlobalGraph(), "TestColorOutputs", 0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
        image_info_t image_info;
        image_info.Format = VK_FORMAT_R8G8B8A8_UNORM;
        image_info.Usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        auto& resource0 = submission.AddColorOutput("TestColorOutput0", image_info);
        auto& outputs = submission.GetColorOutputs();
        CHECK(std::any_of(outputs.cbegin(), outputs.cend(), [resource0](const PipelineResource* output) { return *output == resource0; }));
    }
    TEST_CASE("MultiColorOutputSingleInput") {
        using namespace vpsk;
        PipelineSubmission submission(RenderGraph::GetGlobalGraph(), "TestColorOutputs", 0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
        image_info_t image_info;
        image_info.Format = VK_FORMAT_R8G8B8A8_UNORM;
        image_info.Usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        auto& resource0 = submission.AddColorOutput("TestColorOutput0", image_info);
        auto& gbuffer = RenderGraph::GetGlobalGraph().GetResource("GBuffer");
        auto& resource1 = submission.AddColorOutput("TestColorOutput1", image_info, "GBuffer");
        auto& inputs = submission.GetColorInputs();
        CHECK(std::any_of(inputs.cbegin(), inputs.cend(), [gbuffer](const PipelineResource* resource) {
            return resource != nullptr ? (*resource == gbuffer) : false;
        }));
        auto& outputs = submission.GetColorOutputs();
        CHECK(std::any_of(outputs.cbegin(), outputs.cend(), [resource0](const PipelineResource* output) { return *output == resource0; }));
        CHECK(std::any_of(outputs.cbegin(), outputs.cend(), [resource1](const PipelineResource* output) { return *output == resource1; }));
    }
    TEST_CASE("ResolveOutput") {
        using namespace vpsk;
        PipelineSubmission submission(RenderGraph::GetGlobalGraph(), "TestResolveOutputs", 0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
        image_info_t image_info;
        image_info.Format = VK_FORMAT_R8G8B8A8_UNORM;
        image_info.Usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        image_info.Samples = VK_SAMPLE_COUNT_1_BIT;
        auto& output = submission.AddResolveOutput("ResolveTest", image_info);
        auto outputs_retrieved = submission.GetResolveOutputs();
        CHECK(std::any_of(outputs_retrieved.cbegin(), outputs_retrieved.cend(), [output](const PipelineResource* rsrc) { return *rsrc == output; }));
    }
    TEST_CASE("TextureInput") {
        using namespace vpsk;
        PipelineSubmission submission(RenderGraph::GetGlobalGraph(), "TestTextureInput", 0, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        image_info_t image_info;
        image_info.Format = VK_FORMAT_R8G8B8A8_UNORM;
        image_info.Usage = VK_IMAGE_USAGE_SAMPLED_BIT;
        auto& texture = submission.AddTextureInput("TextureInput", image_info);
        auto texture_inputs = submission.GetTextureInputs();
        CHECK(std::any_of(texture_inputs.cbegin(), texture_inputs.cend(), [texture](const PipelineResource* rsrc) { return *rsrc == texture; }));
    }
    TEST_CASE("StorageTextureOutputNoInput") {
        using namespace vpsk;
        PipelineSubmission submission(RenderGraph::GetGlobalGraph(), "TestStorageOutput", 0, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        image_info_t image_info;
        image_info.Usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        auto& storage = submission.AddStorageTextureOutput("Storage", image_info);
        auto& retrieved = submission.GetStorageTextureOutputs();
        CHECK(std::any_of(retrieved.cbegin(), retrieved.cend(), [storage](const PipelineResource* rsrc) { return *rsrc == storage; }));
    }
    TEST_CASE("StorageTextureOutputWithInput") {
        using namespace vpsk;
        PipelineSubmission submission(RenderGraph::GetGlobalGraph(), "TestStorageOutput", 0, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        image_info_t image_info;
        image_info.Usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        auto& input = RenderGraph::GetGlobalGraph().GetResource("StorageInput");
        auto& output = submission.AddStorageTextureOutput("StorageOutput", image_info, "StorageInput");
        auto& retrieved = submission.GetStorageTextureOutputs();
        CHECK(std::any_of(retrieved.cbegin(), retrieved.cend(), [output](const PipelineResource* rsrc) { return *rsrc == output; }));
    }
}
#endif // DIAMOND_DOGS_TESTING_ENABLED
