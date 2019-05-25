#include "RenderGraph.hpp"
#include "PipelineResource.hpp"
#include "PipelineSubmission.hpp"
#include "DescriptorSet.hpp"
#include "core/ShaderPack.hpp"
#include "core/ShaderResource.hpp"
#include "core/Shader.hpp"
#include "core/ResourceGroup.hpp"
#include "RenderTarget.hpp"
#include "Swapchain.hpp"
#include "RenderingContext.hpp"
#include "ResourceContext.hpp"
#include "DescriptorPack.hpp"
#include <set>
#include <functional>
#include "easylogging++.h"

static constexpr VkPipelineStageFlags ShaderStagesToPipelineStages(const VkShaderStageFlags& flags) {
    VkPipelineStageFlags result = 0;
    if (flags & VK_SHADER_STAGE_VERTEX_BIT) {
        result |= VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
    }
    if (flags & VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT) {
        result |= VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT;
    }
    if (flags & VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT) {
        result |= VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT;
    }
    if (flags & VK_SHADER_STAGE_GEOMETRY_BIT) {
        result |= VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT;
    }
    if (flags & VK_SHADER_STAGE_FRAGMENT_BIT) {
        result |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    if (flags & VK_SHADER_STAGE_COMPUTE_BIT) {
        result |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    }
    return result;
}

static constexpr VkBufferUsageFlags buffer_usage_from_descriptor_type(const VkDescriptorType type) noexcept {
    // I'm using transfer_dst as the base, as I'd prefer to keep all of these buffers on the device
    // as device-local memory when possible
    if (type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER || type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC) {
        return VkBufferUsageFlags{ VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT };
    }
    else if (type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER || type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC) {
        return VkBufferUsageFlags{ VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT };
    }
    else if (type == VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER) {
        return VkBufferUsageFlags{ VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT };
    }
    else if (type == VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER) {
        return VkBufferUsageFlags{ VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT };
    }
    else {
        return VkBufferUsageFlags{ VK_BUFFER_USAGE_FLAG_BITS_MAX_ENUM };
    }
}

static constexpr VkImageUsageFlags image_usage_from_descriptor_type(const VkDescriptorType type) noexcept {
    if (type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER || type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE) {
        return VkImageUsageFlags{ VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT };
    }
    else if (type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE) {
        return VkImageUsageFlags{ VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT };
    }
    else if (type == VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT) {
        return VkImageUsageFlags{ VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT };
    }
    else {
        return VK_IMAGE_USAGE_FLAG_BITS_MAX_ENUM;
    }
}

static constexpr bool is_buffer_type(const VkDescriptorType& type) {
    if (type == VK_DESCRIPTOR_TYPE_SAMPLER ||
        type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE ||
        type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ||
        type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE ||
        type == VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT) {
        return false;
    }
    else {
        return true;
    }
}

std::unique_ptr<RenderTarget> CreateDefaultBackbuffer() {
    auto& context = RenderingContext::Get();
    const vpr::Swapchain* swapchain = context.Swapchain();
    std::unique_ptr<RenderTarget> result = std::make_unique<RenderTarget>();
    result->Create(swapchain->Extent().width, swapchain->Extent().height, swapchain->ColorFormat(), AttachDepthTarget{ true });
    return std::move(result);
}

RenderGraph::RenderGraph() : device(RenderingContext::Get().Device()) {
    renderTargets.emplace(backbufferSource, nullptr);
}

RenderGraph::~RenderGraph() {
    Reset();
}

void RenderGraph::AddShaderPack(const st::ShaderPack * pack) {
    addShaderPackResources(pack);
    addSubmissionsFromPack(pack);
}

PipelineSubmission& RenderGraph::AddPipelineSubmission(const std::string& name, VkPipelineStageFlags stages) {
    auto iter = submissionNameMap.find(name);
    if (iter != std::end(submissionNameMap)) {
        return *submissions[iter->second];
    }
    else {
        size_t idx = submissions.size();
        submissions.emplace_back(std::make_unique<PipelineSubmission>(*this, name, idx, stages));
        submissionNameMap[name] = idx;
        return *submissions.back();
    }
}

PipelineResource& RenderGraph::GetResource(const std::string_view& name) {
    auto iter = resourceNameMap.find(name.data());
    if (iter != resourceNameMap.cend()) {
        const size_t& idx = iter->second;
        return *pipelineResources[idx];
    }
    else {
        const size_t idx = pipelineResources.size();
        pipelineResources.emplace_back(std::make_unique<PipelineResource>(name, idx));
        resourceNameMap[name] = idx;
        return *pipelineResources.back();
    }
}

const DescriptorPack* RenderGraph::GetPackResources(const std::string & name) const {
    if (auto iter = packResources.find(name); iter != std::end(packResources)) {
        return iter->second.get();
    }
    else {
        return nullptr;
    }
}

void RenderGraph::traverseDependencies(const PipelineSubmission & submission, size_t stack_count) {

    if (submission.depthStencilInput != nullptr) {
        dependencyTraversalRecursion(submission, submission.depthStencilInput->SubmissionsWrittenIn(), stack_count, NoCheck{ false }, IgnoreSelf{ false }, MergeDependency{ true });
    }

    for (auto* input : submission.attachmentInputs) {
        bool depends_on_self = (submission.depthStencilOutput != nullptr ? submission.depthStencilInput == input : false);
        if (std::find(std::begin(submission.colorOutputs), std::end(submission.colorOutputs), input) != std::end(submission.colorOutputs)) {
            depends_on_self = true;
        }
        if (!depends_on_self) {
            dependencyTraversalRecursion(submission, input->SubmissionsWrittenIn(), stack_count, NoCheck{ false }, IgnoreSelf{ false }, MergeDependency{ true });
        }
    }

    for (auto* color_input : submission.colorInputs) {
        if (color_input != nullptr) {
            dependencyTraversalRecursion(submission, color_input->SubmissionsWrittenIn(), stack_count, NoCheck{ false }, IgnoreSelf{ false }, MergeDependency{ true });
        }
    }

    for (auto* color_scale_input : submission.colorScaleInputs) {
        if (color_scale_input != nullptr) {
            dependencyTraversalRecursion(submission, color_scale_input->SubmissionsWrittenIn(), stack_count, NoCheck{ false }, IgnoreSelf{ false }, MergeDependency{ false });
        }
    }

   for (const auto& texture_input : submission.genericTextures) {
        dependencyTraversalRecursion(submission, texture_input.Info->SubmissionsWrittenIn(), stack_count, NoCheck{ true }, IgnoreSelf{ false }, MergeDependency{ false });
    }

    for (auto* storage_input : submission.storageInputs) {
        if (storage_input != nullptr) {
            // might be no writers of this, if it's used in a feedback fashion (meaning what?)
            dependencyTraversalRecursion(submission, storage_input->SubmissionsWrittenIn(), stack_count, NoCheck{ true }, IgnoreSelf{ true }, MergeDependency{ false });
            // check for write-after-read hazards, finding if this object is read in other submissions before this one writes to it
            dependencyTraversalRecursion(submission, storage_input->SubmissionsReadIn(), stack_count, NoCheck{ true }, IgnoreSelf{ true }, MergeDependency{ false });
        }
    }

    for (auto* storage_texture_input : submission.storageTextureInputs) {
        if (storage_texture_input != nullptr) {
            dependencyTraversalRecursion(submission, storage_texture_input->SubmissionsWrittenIn(), stack_count, NoCheck{ false }, IgnoreSelf{ false }, MergeDependency{ false });
        }
    }

    for (auto* texel_input : submission.texelBufferInputs) {
        if (texel_input != nullptr) {
            dependencyTraversalRecursion(submission, texel_input->SubmissionsWrittenIn(), stack_count, NoCheck{ false }, IgnoreSelf{ true }, MergeDependency{ false });
        }
    }

    for (const auto& generic_buffer : submission.genericBuffers) {
        dependencyTraversalRecursion(submission, generic_buffer.Info->SubmissionsWrittenIn(), stack_count, NoCheck{ true }, IgnoreSelf{ false }, MergeDependency{ false });
    }

}

void RenderGraph::dependencyTraversalRecursion(const PipelineSubmission & curr, const std::unordered_set<size_t>& passes, size_t stack_count, const NoCheck no_check, const IgnoreSelf ignore_self, const MergeDependency merge_dependency) {
    
    if (!no_check && passes.empty()) {
        LOG(ERROR) << "Requested checking of a resource during dependency traversal, but current resource is not written to in any passes!";
        throw std::logic_error("Resource is not written to by any passes.");
    }

    if (stack_count > NumSubmissions()) {
        LOG(ERROR) << "Stuck in dependency traversal recursion loop.";
        throw std::runtime_error("Stuck in a recursive loop.");
    }

    for (auto& submission : passes) {
        if (submission != curr.idx) {
            submissionDependencies[curr.idx].insert(submission);
        }
    }

    if (merge_dependency) {
        for (auto& submission : passes) {
            if (submission != curr.idx) {
                submissionMergeDependencies[curr.idx].insert(submission);
            }
        }
    }

    ++stack_count;

    for (auto& pushed_submission : passes) {
        if (ignore_self && (pushed_submission == curr.idx)) {
            continue;
        }
        else if (pushed_submission == curr.idx) {
            LOG(ERROR) << "Found a submission resource with a cyclic dependency on itself during dependency traversal recursion step.";
            throw std::logic_error("Submission resource has a self-dependency loop.");
        }

        submissionStack.push_back(pushed_submission);
        const auto& next_submission = *submissions[pushed_submission];
        traverseDependencies(next_submission, stack_count);
    }

}

void RenderGraph::addShaderPackResources(const st::ShaderPack* pack) {
    shaderPacks.emplace_back(pack);
    packResources.emplace("", std::make_unique<DescriptorPack>(this, pack));
    createPipelineResourcesFromPack(pack);
}

buffer_info_t RenderGraph::createPipelineResourceBufferInfo(const st::ShaderResource* resource) const
{
    return buffer_info_t{
        0u,
        buffer_usage_from_descriptor_type(resource->DescriptorType()),
        resource->Format()
    };
}

image_info_t RenderGraph::createPipelineResourceImageInfo(const st::ShaderResource* resource) const
{
    image_info_t result;
    result.Format = resource->Format();
    result.Usage = image_usage_from_descriptor_type(resource->DescriptorType());
    return result;
}

void RenderGraph::Bake() {
    for (auto& submission : submissions) {
        submission->ValidateSubmission();
    }

    auto backbuffer_iter = resourceNameMap.find(backbufferSource);
    if (backbuffer_iter == resourceNameMap.end()) {
        throw std::logic_error("No backbuffer source set for Rendegraph!");
    }

    submissionDependencies.clear(); submissionDependencies.shrink_to_fit();
    submissionDependencies.resize(submissions.size());
    submissionMergeDependencies.clear(); submissionMergeDependencies.shrink_to_fit();
    submissionMergeDependencies.resize(submissions.size());

    auto& backbuffer_resource = *pipelineResources[backbuffer_iter->second];

    if (backbuffer_resource.SubmissionsWrittenIn().empty()) {
        throw std::logic_error("No writes occurred to the backbuffer!");
    }

    for (auto& submission : backbuffer_resource.SubmissionsWrittenIn()) {
        submissionStack.push_back(submission);
    }

    std::vector<size_t> temp_submission_stack = submissionStack;
    for (auto& pushed_submission : submissionStack) {
        size_t stack_count = 0;
        const auto& cur_submission = *submissions[pushed_submission];
        traverseDependencies(cur_submission, 0);
    }

    std::reverse(std::begin(submissionStack), std::end(submissionStack));

}

void RenderGraph::Reset() {
    renderTargets.clear();
    pipelineResources.clear();
    packResources.clear();
}

void RenderGraph::AddTagFunction(const std::string & tag, delegate_t<void(PipelineSubmission&)> fn) {
    tagFunctionsMap.emplace(tag, fn);
}

void RenderGraph::SetBackbufferSource(const std::string & name) {
    backbufferSource = name;
}

resource_dimensions_t RenderGraph::GetResourceDimensions(const PipelineResource & rsrc) {
    resource_dimensions_t result;
    result.BufferInfo = rsrc.GetBufferInfo();
    return result;
}

size_t RenderGraph::NumSubmissions() const noexcept {
    return submissions.size();
}

const RenderTarget * RenderGraph::GetBackbuffer() const noexcept {
    const auto& backbuffer = renderTargets.at(backbufferSource);
    return backbuffer.get();
}

const vpr::Device* RenderGraph::GetDevice() const noexcept {
    return device;
}

RenderGraph& RenderGraph::GetGlobalGraph() {
    static RenderGraph graph;
    return graph;
}

void RenderGraph::createPipelineResourcesFromPack(const st::ShaderPack* pack) {
    std::unordered_map<std::string, std::vector<const st::ShaderResource*>> resources;

    std::vector<std::string> resource_group_names;
    {
        st::dll_retrieved_strings_t names = pack->GetResourceGroupNames();
        for (size_t i = 0; i < names.NumStrings; ++i) {
            resource_group_names.emplace_back(names[i]);
        }
    }

    for (auto& name : resource_group_names) {
        size_t num_rsrc = 0;
        const st::ResourceGroup* resource_group = pack->GetResourceGroup(name.c_str());
        resource_group->GetResourcePtrs(&num_rsrc, nullptr);
        std::vector<const st::ShaderResource*> group(num_rsrc);
        resource_group->GetResourcePtrs(&num_rsrc, group.data());
        resources.emplace(name.c_str(), group);
    }

    for (const auto& rsrc_group : resources) {
        for (const auto& st_rsrc : rsrc_group.second) {
            auto& resource = GetResource(st_rsrc->Name());
            resource.SetDescriptorType(st_rsrc->DescriptorType());
            resource.SetParentSetName(st_rsrc->ParentGroupName());
            if (is_buffer_type(st_rsrc->DescriptorType())) {
                resource.SetInfo(createPipelineResourceBufferInfo(st_rsrc));
            }
            else if (st_rsrc->DescriptorType() == VK_DESCRIPTOR_TYPE_SAMPLER) {
                // We don't need to create more info for this.
                continue;
            }
            else {
                resource.SetInfo(createPipelineResourceImageInfo(st_rsrc));
            }

        }
    }
}

void RenderGraph::addResourceUsagesToSubmission(PipelineSubmission & submission, const std::vector<st::ResourceUsage>& usages) {

    auto add_storage_image = [&](const PipelineResource* rsrc, const st::ResourceUsage& usage) {
        switch (usage.AccessModifier()) {
        case st::access_modifier::Read:
            submission.AddStorageTextureInput(rsrc->Name(), rsrc->GetImageInfo());
            break;
        case st::access_modifier::Write:
            submission.AddStorageTextureOutput(rsrc->Name(), rsrc->GetImageInfo());
            break;
        case st::access_modifier::ReadWrite:
            submission.AddStorageTextureRW(rsrc->Name(), rsrc->GetImageInfo());
            break;
        default:
            throw std::domain_error("Invalid access modifier for resource usage.");
        }
    };

    auto add_storage_buffer_type = [&](const PipelineResource* rsrc, const st::ResourceUsage& usage) {
        switch (usage.AccessModifier()) {
        case st::access_modifier::Read:
            submission.AddStorageReadOnlyInput(rsrc->Name());
            break;
        case st::access_modifier::Write:
            submission.AddStorageOutput(rsrc->Name(), rsrc->GetBufferInfo());
            break;
        case st::access_modifier::ReadWrite:
            submission.AddStorageOutput(rsrc->Name(), rsrc->GetBufferInfo(), rsrc->Name());
            break;
        default:
            throw std::domain_error("Invalid access modifier for resource usage.");
        }
    };

    auto add_storage_texel_buffer_type = [&](const PipelineResource* rsrc, const st::ResourceUsage& usage) {
        switch (usage.AccessModifier()) {
        case st::access_modifier::Read:
            submission.AddTexelBufferInput(rsrc->Name(), rsrc->GetBufferInfo());
            break;
        case st::access_modifier::Write:
            submission.AddTexelBufferOutput(rsrc->Name(), rsrc->GetBufferInfo());
            break;
        case st::access_modifier::ReadWrite:
            submission.AddTexelBufferRW(rsrc->Name(), rsrc->GetBufferInfo());
            break;
        default:
            throw std::domain_error("Invalid access modifier for st::ResourceUsage.");
        }
    };

    for (const auto& usage : usages) {
        auto iter = resourceNameMap.find(usage.BackingResource()->Name());
        if (iter == std::end(resourceNameMap)) {
            throw std::out_of_range("Couldn't find PipelineResource matching a resource usage!");
        }
        auto& resource = pipelineResources[iter->second];

        switch (usage.Type()) {
        case VK_DESCRIPTOR_TYPE_SAMPLER:
            // Don't need to do anything, these are immutable and don't need guarding
            break;
        case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
            submission.AddTextureInput(resource->Name(), resource->SubmissionStageFlags(submission.idx));
            break;
        case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
            submission.AddTextureInput(resource->Name(), resource->SubmissionStageFlags(submission.idx));
            break;
        case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
            add_storage_image(resource.get(), usage);
            break;
        case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
            submission.AddUniformTexelBufferInput(resource->Name(), resource->SubmissionStageFlags(submission.idx));
            break;
        case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
            add_storage_texel_buffer_type(resource.get(), usage);
            break;
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
            submission.AddUniformInput(resource->Name());
            break;
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
            add_storage_buffer_type(resource.get(), usage);
            break;
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
            submission.AddUniformInput(resource->Name());
            break;
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
            add_storage_buffer_type(resource.get(), usage);
            break;
        case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
            submission.AddInputAttachment(resource->Name());
            break;
        default:
            throw std::domain_error("Invalid VkDescriptorType for ResourceUsage object");
        }
    }

    // Need to parse output vertex attributes to add output color attachments.

}

bool is_depth_format(const VkFormat& fmt) noexcept {
    const static std::set<VkFormat> depth_formats{
        VK_FORMAT_D16_UNORM,
        VK_FORMAT_D16_UNORM_S8_UINT,
        VK_FORMAT_D24_UNORM_S8_UINT,
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D32_SFLOAT_S8_UINT
    };
    return depth_formats.count(fmt) != 0;
}

void RenderGraph::addSingleGroup(const std::string & name, const st::Shader* group) {
    PipelineSubmission& submission = AddPipelineSubmission(name, ShaderStagesToPipelineStages(group->Stages()));

    const size_t num_sets_in_submissions = group->GetNumSetsRequired();
    for (size_t i = 0; i < num_sets_in_submissions; ++i) {
        size_t num_rsrcs = 0;
        group->GetResourceUsages(i, &num_rsrcs, nullptr);
        std::vector<st::ResourceUsage> usages(num_rsrcs);
        group->GetResourceUsages(i, &num_rsrcs, usages.data());
        addResourceUsagesToSubmission(submission, usages);
    }

    std::vector<std::string> tags;
    {
        st::dll_retrieved_strings_t tag_strings = group->GetTags();
        for (size_t i = 0; i < tag_strings.NumStrings; ++i) {
            tags.emplace_back(tag_strings[i]);
        }
    }

    if (!tags.empty()) {
        // We now have some behavior defined by tags. What could it be?
        submission.SetTags(tags);
        for (auto& tag : tags) {
            if (tagFunctionsMap.count(tag) != 0) {
                tagFunctionsMap.at(tag)(submission);
            }
        }
    }


    {
        // Look for output attributes
        size_t num_attrs = 0;
        group->GetOutputAttributes(VK_SHADER_STAGE_FRAGMENT_BIT, &num_attrs, nullptr);
        if (num_attrs != 0) {
            std::vector<st::VertexAttributeInfo> attributes(num_attrs);
            group->GetOutputAttributes(VK_SHADER_STAGE_FRAGMENT_BIT, &num_attrs, attributes.data());
            for (auto& attrib : attributes) {
                const std::string attrib_name(attrib.Name());
                if (attrib_name == backbufferSource) {
                    auto& backbuffer = renderTargets.at(backbufferSource);
                    submission.AddColorOutput("backbuffer", backbuffer->GetImageInfo());
                }
                else if (attrib.GetAsFormat() != VK_FORMAT_UNDEFINED) {
                    auto& resource = GetResource(attrib.Name());
                    image_info_t info;
                    info.Format = attrib.GetAsFormat();
                    info.SizeClass = image_info_t::size_class::SwapchainRelative;
                    if (!is_depth_format(info.Format)) {
                        submission.AddColorOutput(resource.Name(), info);
                    }
                    else {
                        submission.SetDepthStencilInput(resource.Name());
                    }
                }
            }
        }
    }
        
}

void RenderGraph::addSubmissionsFromPack(const st::ShaderPack* pack) {
    std::vector<std::string> pack_names;
    {
        auto pack_retrieved = pack->GetShaderGroupNames();
        for (size_t i = 0; i < pack_retrieved.NumStrings; ++i) {
            pack_names.emplace_back(pack_retrieved[i]);
        }
    }

    for (auto&& name : pack_names) {
        addSingleGroup(name, pack->GetShaderGroup(name.c_str()));
    }

}
