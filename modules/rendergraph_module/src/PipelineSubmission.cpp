#include "PipelineSubmission.hpp"
#include "RenderGraph.hpp"
#include "easylogging++.h"
#include "PipelineLayout.hpp"
#include "PipelineCache.hpp"

PipelineSubmission::PipelineSubmission(RenderGraph& rgraph, std::string _name, size_t _idx, VkPipelineStageFlags _stages) 
    : graph(rgraph), name(std::move(_name)), idx(std::move(_idx)), stages(std::move(_stages)) {}

PipelineSubmission::~PipelineSubmission() {}

PipelineResource& PipelineSubmission::SetDepthStencilInput(const std::string& name) {
    auto& resource = graph.GetResource(name);
    resource.AddUsedPipelineStages(idx, stages);
    resource.ReadBySubmission(idx);
    depthStencilInput = &resource;
    return resource;
}

PipelineResource& PipelineSubmission::SetDepthStencilOutput(const std::string& name, image_info_t info) {
    auto& resource = graph.GetResource(name);
    resource.AddUsedPipelineStages(idx, stages);
    resource.WrittenBySubmission(idx);
    if (!(info.Usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)) {
        LOG(ERROR) << "Tried to add a depth stencil output, but image info structure has invalid usage flags!";
        throw std::runtime_error("Invalid image usage flags.");
    }
    resource.SetInfo(info);
    depthStencilOutput = &resource;
    return resource;
}

PipelineResource& PipelineSubmission::AddInputAttachment(const std::string& name) {
    auto& resource = graph.GetResource(name);
    resource.AddUsedPipelineStages(idx, stages);
    resource.ReadBySubmission(idx);
    attachmentInputs.emplace_back(&resource);
    return resource;
}

PipelineResource& PipelineSubmission::AddHistoryInput(const std::string& name) {
    auto& resource = graph.GetResource(name);
    resource.AddUsedPipelineStages(idx, stages);
    historyInputs.emplace_back(&resource);
    return resource;
}

PipelineResource& PipelineSubmission::AddColorOutput(const std::string& name, image_info_t info, const std::string& input) {
    auto& resource = graph.GetResource(name);
    resource.AddUsedPipelineStages(idx, stages);
    resource.WrittenBySubmission(idx);
    if (!(info.Usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)) {
        LOG(ERROR) << "Tried to add a color attachment input, but given image info object lacks requisite image usage flags!";
        throw std::runtime_error("Invalid image usage flags.");
    }
    resource.SetInfo(info);
    colorOutputs.emplace_back(&resource);
    if (!input.empty()) {
        auto& input_resource = graph.GetResource(input);
        input_resource.ReadBySubmission(idx);
        colorInputs.emplace_back(&input_resource);
        colorScaleInputs.emplace_back(&input_resource);
    }
    else {
        colorInputs.emplace_back(nullptr);
        colorScaleInputs.emplace_back(nullptr);
    }
    return resource;
}

PipelineResource& PipelineSubmission::AddResolveOutput(const std::string& name, image_info_t info) {
    auto& resource = graph.GetResource(name);
    resource.AddUsedPipelineStages(idx, stages);
    resource.WrittenBySubmission(idx);        
    if (info.Samples != VK_SAMPLE_COUNT_1_BIT) {
        LOG(ERROR) << "Image info for resolve output attachment has invalid sample count flags value!";
        throw std::runtime_error("Invalid sample count flags.");
    }
    resource.SetInfo(info);
    resolveOutputs.emplace_back(&resource);
    return resource;
}

PipelineResource& PipelineSubmission::AddTextureInput(const std::string& name, image_info_t info) {
    auto& resource = graph.GetResource(name);
    resource.AddUsedPipelineStages(idx, stages);
    if (!(info.Usage & VK_IMAGE_USAGE_SAMPLED_BIT)) {
        LOG(ERROR) << "Attempted to add a texture input, but image info structure lacks requisite image usage flags!";
        throw std::runtime_error("Tried to add texture input when image has invalid usage flags.");
    }
    resource.SetInfo(info);
    resource.ReadBySubmission(idx);
    textureInputs.emplace_back(&resource);
    return resource;
}

PipelineResource & PipelineSubmission::AddStorageTextureInput(const std::string & name, image_info_t info, const std::string& output) {
    auto& resource = graph.GetResource(name);
    resource.AddUsedPipelineStages(idx, stages);
    resource.WrittenBySubmission(idx);
    if (!(info.Usage & VK_IMAGE_USAGE_STORAGE_BIT)) {

    }
    resource.SetInfo(info);
    resource.SetStorage(true);
    storageTextureInputs.emplace_back(&resource);
    if (!output.empty()) {
        auto& output_resource = graph.GetResource(output);
        output_resource.WrittenBySubmission(idx);
        storageTextureOutputs.emplace_back(&output_resource);
    }
    else {
        storageTextureOutputs.emplace_back(nullptr);
    }
    return resource;
}

PipelineResource& PipelineSubmission::AddStorageTextureOutput(const std::string& name, image_info_t info, const std::string& input) {
    auto& resource = graph.GetResource(name);
    resource.AddUsedPipelineStages(idx, stages);
    resource.WrittenBySubmission(idx);
    if (!(info.Usage & VK_IMAGE_USAGE_STORAGE_BIT)) {

    }
    resource.SetInfo(info);
    resource.SetStorage(true);
    storageTextureOutputs.emplace_back(&resource);
    if (!input.empty()) {
        auto& input_resource = graph.GetResource(input);
        input_resource.ReadBySubmission(idx);
        storageTextureInputs.emplace_back(&input_resource);
    }
    else {
        storageTextureInputs.emplace_back(nullptr);
    }
    return resource;
}

PipelineResource& PipelineSubmission::AddStorageTextureRW(const std::string& name, image_info_t info) {
    auto& resource = graph.GetResource(name);
    resource.AddUsedPipelineStages(idx, stages);
    resource.ReadBySubmission(idx);
    resource.WrittenBySubmission(idx);
    resource.SetStorage(true);
    storageTextureOutputs.emplace_back(&resource);
    storageTextureInputs.emplace_back(&resource);
    return resource;
}

PipelineResource& PipelineSubmission::AddUniformInput(const std::string& name) {
    auto& resource = graph.GetResource(name);
    resource.AddUsedPipelineStages(idx, stages);
    resource.ReadBySubmission(idx);
    uniformInputs.emplace_back(&resource);
    return resource;
}

PipelineResource& PipelineSubmission::AddStorageOutput(const std::string& name, buffer_info_t info, const std::string& input) {
    auto& resource = graph.GetResource(name);
    resource.AddUsedPipelineStages(idx, stages);
    resource.WrittenBySubmission(idx);
    resource.SetInfo(info);
    resource.SetStorage(true);
    storageOutputs.emplace_back(&resource);
    if (!input.empty()) {
        auto& input_resource = graph.GetResource(input);
        input_resource.ReadBySubmission(idx);
        storageInputs.emplace_back(&input_resource);
    }
    else {
        storageInputs.emplace_back(nullptr);
    }
    return resource;
}

PipelineResource& PipelineSubmission::AddStorageRW(const std::string& name, buffer_info_t info) {
    auto& resource = graph.GetResource(name);
    resource.AddUsedPipelineStages(idx, stages);
    resource.WrittenBySubmission(idx);
    resource.ReadBySubmission(idx);
    resource.SetInfo(info);
    resource.SetStorage(true);
    storageInputs.emplace_back(&resource);
    storageOutputs.emplace_back(&resource);
    return resource;
}

PipelineResource& PipelineSubmission::AddStorageReadOnlyInput(const std::string& name) {
    auto& resource = graph.GetResource(name);
    resource.AddUsedPipelineStages(idx, stages);
    resource.ReadBySubmission(idx);
    storageReadOnlyInputs.emplace_back(&resource);
    return resource;
}

const std::vector<PipelineResource*>& PipelineSubmission::GetColorOutputs() const noexcept {
    return colorOutputs;
}

const std::vector<PipelineResource*>& PipelineSubmission::GetResolveOutputs() const noexcept {
    return resolveOutputs;
}

const std::vector<PipelineResource*>& PipelineSubmission::GetColorInputs() const noexcept {
    return colorInputs;
}

const std::vector<PipelineResource*>& PipelineSubmission::GetColorScaleInputs() const noexcept {
    return colorScaleInputs;
}

const std::vector<PipelineResource*>& PipelineSubmission::GetTextureInputs() const noexcept {
    return textureInputs;
}

const std::vector<PipelineResource*>& PipelineSubmission::GetStorageTextureInputs() const noexcept {
    return storageTextureInputs;
}

const std::vector<PipelineResource*>& PipelineSubmission::GetStorageTextureOutputs() const noexcept {
    return storageTextureOutputs;
}

const std::vector<PipelineResource*>& PipelineSubmission::GetAttachmentInputs() const noexcept {
    return attachmentInputs;
}

const std::vector<PipelineResource*>& PipelineSubmission::GetHistoryInputs() const noexcept {
    return historyInputs;
}

const std::vector<PipelineResource*>& PipelineSubmission::GetUniformInputs() const noexcept {
    return uniformInputs;
}

const std::vector<PipelineResource*>& PipelineSubmission::GetStorageOutputs() const noexcept {
    return storageOutputs;
}

const std::vector<PipelineResource*>& PipelineSubmission::GetStorageReadOnlyInputs() const noexcept {
    return storageReadOnlyInputs;
}

const std::vector<PipelineResource*>& PipelineSubmission::GetStorageInputs() const noexcept {
    return storageInputs;
}

const std::vector<std::string>& PipelineSubmission::GetTags() const noexcept {
    return submissionTags;
}

const PipelineResource * PipelineSubmission::GetDepthStencilInput() const noexcept {
    return depthStencilInput;
}

const PipelineResource * PipelineSubmission::GetDepthStencilOutput() const noexcept {
    return depthStencilOutput;
}

void PipelineSubmission::SetIdx(size_t _idx) {
    idx = std::move(_idx);
}

void PipelineSubmission::SetPhysicalPassIdx(size_t idx) {
    physicalPassIdx = std::move(idx);
}

void PipelineSubmission::SetStages(VkPipelineStageFlags _stages) {
    stages = std::move(_stages);
}

void PipelineSubmission::SetName(std::string _name) {
    name = std::move(_name);
}

void PipelineSubmission::AddTag(std::string _tag) {
    submissionTags.emplace_back(std::move(_tag));
}

void PipelineSubmission::SetTags(std::vector<std::string> _tags) {
    submissionTags = std::move(_tags);
}

const size_t& PipelineSubmission::GetIdx() const noexcept {
    return idx;
}

const size_t& PipelineSubmission::GetPhysicalPassIdx() const noexcept {
    return physicalPassIdx;
}

const VkPipelineStageFlags& PipelineSubmission::GetStages() const noexcept {
    return stages;
}

const std::string& PipelineSubmission::Name() const noexcept {
    return name;
}

bool PipelineSubmission::NeedRenderPass() const noexcept {
    if (!needPassCb) {
        return true;
    }
    else {
        return needPassCb();
    }
}

bool PipelineSubmission::GetClearColor(size_t idx, VkClearColorValue* value) noexcept {
    if (!getClearColorValueCb) {
        return false;
    }
    else {
        return getClearColorValueCb(idx, value);
    }
}

bool PipelineSubmission::GetClearDepth(VkClearDepthStencilValue* value) const noexcept {
    if (!getClearDepthValueCb) {
        return false;
    }
    else {
        return getClearDepthValueCb(value);
    }
}

bool PipelineSubmission::ValidateSubmission() {

    auto attachment_count_err = [&](const std::string& attachment_type) {
        LOG(ERROR) << "Pipeline submission " << name << " has a mismatched quantity of " << attachment_type << " inputs and outputs!";
        throw std::logic_error(std::string("Invalid " + attachment_type + " counts"));
    };

    if (colorInputs.size() != colorOutputs.size()) {
        attachment_count_err("color attachment");
    }
    if (storageInputs.size() != storageOutputs.size()) {
        attachment_count_err("storage buffer");
    }
    if (storageTextureInputs.size() != storageTextureOutputs.size()) {
        attachment_count_err("storage texture");
    }
    if (!resolveOutputs.empty()) {
        if (resolveOutputs.size() != colorOutputs.size()) {
            attachment_count_err("resolve attachment");
        }
    }

    for (size_t i = 0; i < colorInputs.size(); ++i) {
        if (colorInputs[i] == nullptr) {
            continue;
        }

        if (graph.GetResourceDimensions(*colorInputs[i]) != graph.GetResourceDimensions(*colorOutputs[i])) {
            // Need to scale/modify color input before it becomes the output
            MakeColorInputScaled(i);
        }

    }

    if (!storageOutputs.empty()) {
        for (size_t i = 0; i < storageOutputs.size(); ++i) {
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

    if ((depthStencilInput != nullptr) && (depthStencilOutput != nullptr)) {
        if (depthStencilInput->GetInfo() != depthStencilOutput->GetInfo()) {
            LOG(ERROR) << "Depth stencil input and output are mismatched in submission " << name << "!";
            throw std::logic_error("Mismatched depth stencil input-output!");
        }
    }

    return true;
}

void PipelineSubmission::MakeColorInputScaled(const size_t & idx) {
    std::swap(colorInputs[idx], colorScaleInputs[idx]);
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
