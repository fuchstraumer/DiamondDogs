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
#include "LogicalDevice.hpp"
#include "VkDebugUtils.hpp"

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

Descriptor* DescriptorPack::RetrieveDescriptor(const std::string & rsrc_group_name) {
    auto iter = rsrcGroupToIdxMap.find(rsrc_group_name);
    if (iter == rsrcGroupToIdxMap.end()) {
        return nullptr;
    }
    else {
        return descriptors[iter->second].get();
    }
}

DescriptorBinder DescriptorPack::RetrieveBinder(const std::string& shader_group) {
    auto iter = shaderGroupNameIdxMap.find(shader_group);
    if (iter == shaderGroupNameIdxMap.end()) {
        throw std::out_of_range("Shader group requested is not in map.");
    }

    size_t sg_idx = iter->second;
    auto& indices = shaderGroupResourceGroupUsages[sg_idx];

    DescriptorBinder result(indices.size(), pipelineLayouts[sg_idx]->vkHandle());
    for (size_t i = 0; i < indices.size(); ++i) {
        result.AddDescriptor(i, resourceGroups[indices[i]]->Name(), descriptors[indices[i]].get());
    }

    return std::move(result);
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
        const std::string group_name{ group->Name() };

        descriptorTemplates.emplace_back(std::make_unique<DescriptorTemplate>(group->Name()));
		if constexpr (VTF_USE_DEBUG_INFO && VTF_VALIDATION_ENABLED)
		{
			auto SetObjectNameFn = RenderingContext::Get().Device()->DebugUtilsHandler().vkSetDebugUtilsObjectName;
			const VkDevice device_handle = RenderingContext::Get().Device()->vkHandle();
			const std::string template_object_name = group_name + std::string("_DescriptorTemplate");
			const VkDebugUtilsObjectNameInfoEXT template_name_info{
				VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
				nullptr,
				VK_OBJECT_TYPE_DESCRIPTOR_UPDATE_TEMPLATE,
				(uint64_t)descriptorTemplates.back()->UpdateTemplate(),
				template_object_name.c_str()
			};
			SetObjectNameFn(device_handle, &template_name_info);
			const std::string set_layout_name = group_name + std::string("_DescriptorSetLayout");
			const VkDebugUtilsObjectNameInfoEXT set_layout_name_info{
				VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
				nullptr,
				VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT,
				(uint64_t)descriptorTemplates.back()->UpdateTemplate(),
				set_layout_name.c_str()
			};
			SetObjectNameFn(device_handle, &set_layout_name_info);
		}
        auto* templ = descriptorTemplates.back().get();

        size_t num_rsrcs{ 0u };
        group->GetResourcePtrs(&num_rsrcs, nullptr);
        std::vector<const st::ShaderResource*> resource_ptrs(num_rsrcs);
        group->GetResourcePtrs(&num_rsrcs, resource_ptrs.data());

        std::unordered_map<std::string, size_t> binding_locs;
        
        for (const auto* rsrc : resource_ptrs) {
            templ->AddLayoutBinding(rsrc->BindingIndex(), rsrc->DescriptorType());
            binding_locs.emplace(rsrc->Name(), rsrc->BindingIndex());
        }

        bindingLocations.emplace(group_name, std::move(binding_locs));

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
            // this map stores descriptor indices in the right binding order for us to use later
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

    descriptors.resize(resourceGroups.size());

    for (const auto* group : resourceGroups) {
        const std::string group_name{ group->Name() };
        size_t idx = rsrcGroupToIdxMap.at(group_name);

        descriptors[idx] = createDescriptor(group_name, std::move(bindingLocations.at(group_name)));
    }

}

std::unique_ptr<Descriptor> DescriptorPack::createDescriptor(const std::string & rsrc_group_name, std::unordered_map<std::string, size_t>&& binding_locs) {
    auto iter = rsrcGroupToIdxMap.find(rsrc_group_name);
    if (iter == std::end(rsrcGroupToIdxMap)) {
        throw std::domain_error("Invalid resource group name: not in container!");
    }

    const vpr::Device* device_ptr = RenderingContext::Get().Device();
    const size_t idx = iter->second;

    return std::make_unique<Descriptor>(device_ptr, resourceGroups[idx]->DescriptorCounts(), rsrcGroupUseFrequency[idx], descriptorTemplates[idx].get(), std::move(binding_locs));
}
