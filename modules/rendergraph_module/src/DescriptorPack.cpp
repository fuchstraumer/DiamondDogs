#include "DescriptorPack.hpp"
#include "Descriptor.hpp"
#include "RenderGraph.hpp"
#include "RenderingContext.hpp"
// ShaderTools
#include "core/ShaderPack.hpp"
#include "core/ShaderResource.hpp"
#include "core/ResourceUsage.hpp"
#include "core/ResourceGroup.hpp"
#include "core/Shader.hpp"
// VPR
#include "PipelineLayout.hpp"
#include "CreateInfoBase.hpp"

DescriptorPack::DescriptorPack(RenderGraph* _graph, const st::ShaderPack * pack) : shaderPack(pack), graph(_graph) {
    retrieveResourceGroups();
    createDescriptorTemplates();
    parseGroupBindingInfo();
    createDescriptors();
}

DescriptorPack::~DescriptorPack() {}

VkPipelineLayout DescriptorPack::PipelineLayout(const std::string& name) const {
    const size_t idx = shaderGroupNameIdxMap.at(name);
    return pipelineLayouts[idx]->vkHandle();
}

size_t DescriptorPack::BindingLocation(const std::string & name) const noexcept {
    return resourceBindingLocations.at(name);
}

void DescriptorPack::retrieveResourceGroups() {
    auto group_names_dll = shaderPack->GetResourceGroupNames();
    rsrcGroupUseFrequency.resize(group_names_dll.NumStrings, 0u);
    for (size_t i = 0; i < group_names_dll.NumStrings; ++i) {
        resourceGroups.emplace_back(shaderPack->GetResourceGroup(group_names_dll[i]));
        rsrcGroupToIdxMap.emplace(std::string{ group_names_dll[i] }, resourceGroups.size() - 1u);
    }
}

void DescriptorPack::createDescriptorTemplates() {

    for (const auto* group : resourceGroups) {

        descriptorTemplates.emplace_back(std::make_unique<DescriptorTemplate>(group->Name()));
        auto* templ = descriptorTemplates.back().get();

        size_t num_rsrcs{ 0u };
        group->GetResourcePtrs(&num_rsrcs, nullptr);
        std::vector<const st::ShaderResource*> resource_ptrs(num_rsrcs);
        group->GetResourcePtrs(&num_rsrcs, resource_ptrs.data());
        
        for (const auto* rsrc : resource_ptrs) {
            templ->AddLayoutBinding(rsrc->BindingIndex(), rsrc->DescriptorType());
        }

    }

}

void DescriptorPack::parseGroupBindingInfo() {
    
    std::vector<std::string> shader_group_strs;

    {
        auto shader_group_names = shaderPack->GetShaderGroupNames();
        for (size_t i = 0; i < shader_group_names.NumStrings; ++i) {
            shader_group_strs.emplace_back(shader_group_names[i]);
        }
    }

    shaderGroupResourceGroupUsages.resize(shader_group_strs.size());
    shaderGroups.resize(shader_group_strs.size());
    pipelineLayouts.resize(shader_group_strs.size());

    for (const auto& str : shader_group_strs) {
        auto emplaced = shaderGroupNameIdxMap.emplace(str, shaderGroupNameIdxMap.size());
        const size_t idx = emplaced.first->second;
        shaderGroups[idx] = shaderPack->GetShaderGroup(str.c_str());
    }

    for (const auto& str : shader_group_strs) {
        createPipelineLayout(str);
    }

}

void DescriptorPack::createPipelineLayout(const std::string & name) {
    const size_t idx = shaderGroupNameIdxMap.at(name);
    const st::Shader* shader = shaderGroups[idx];

    size_t num_stages = 0;
    shader->GetShaderStages(&num_stages, nullptr);
    std::vector<st::ShaderStage> stages(num_stages, st::ShaderStage("null_name", VK_SHADER_STAGE_ALL));
    shader->GetShaderStages(&num_stages, stages.data());
    
    std::vector<VkPushConstantRange> push_constant_ranges;

    for (const auto& stage : stages) {
        auto pc_info = shader->GetPushConstantInfo(stage.GetStage());
        if (pc_info.Stages() != VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM) {
            push_constant_ranges.emplace_back((VkPushConstantRange)pc_info);
        }
    }

    std::vector<std::string> used_block_strs;
    std::vector<VkDescriptorSetLayout> set_layouts;
    {
        auto used_blocks = shader->GetUsedResourceBlocks();

        set_layouts.resize(used_blocks.NumStrings);
        shaderGroupResourceGroupUsages[idx].resize(used_blocks.NumStrings);

        for (size_t i = 0; i < used_blocks.NumStrings; ++i) {
            size_t container_idx = rsrcGroupToIdxMap.at(used_blocks[i]);
            size_t set_idx_in_shader = static_cast<size_t>(shader->ResourceGroupSetIdx(used_blocks[i]));
            // this group stores descriptor indices in the right binding order for us to use later
            shaderGroupResourceGroupUsages[idx][set_idx_in_shader] = container_idx;
            set_layouts[set_idx_in_shader] = descriptorTemplates[container_idx]->SetLayout();

            // update use freqeuncy so we can make the descriptors the right (approximate) initial size
            rsrcGroupUseFrequency[rsrcGroupToIdxMap.at(used_blocks[i])] += 1;
        }
    }

    pipelineLayouts[idx] = std::make_unique<vpr::PipelineLayout>(RenderingContext::Get().Device()->vkHandle());
    pipelineLayouts[idx]->Create(push_constant_ranges.size(), push_constant_ranges.data(), set_layouts.size(), set_layouts.data());

}

void DescriptorPack::createDescriptors() {
    const vpr::Device* device_ptr = RenderingContext::Get().Device();

    for (size_t i = 0; i < resourceGroups.size(); ++i) {
        descriptors.emplace_back(std::make_unique<Descriptor>(device_ptr, resourceGroups[i]->DescriptorCounts(), rsrcGroupUseFrequency[i], descriptorTemplates[i].get()));
    }

}
