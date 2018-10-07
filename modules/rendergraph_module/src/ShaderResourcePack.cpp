#include "ShaderResourcePack.hpp"
#include "DescriptorPool.hpp"
#include "DescriptorSet.hpp"
#include "DescriptorSetLayout.hpp"
#include "core/ShaderPack.hpp"
#include "RenderGraph.hpp"
#include "RenderingContext.hpp"
#include "ResourceContext.hpp"
#include "PipelineLayout.hpp"
#include "LogicalDevice.hpp"
#include "CreateInfoBase.hpp"
#include "core/ShaderResource.hpp"
#include "core/ResourceUsage.hpp"
#include "core/ResourceGroup.hpp"
#include "core/Shader.hpp"

ShaderResourcePack::ShaderResourcePack(RenderGraph* _graph, const st::ShaderPack * pack) : shaderPack(pack), graph(_graph) {
    getGroupNames();
    createDescriptorPool();
    createSets();
    parseGroupBindingInfo();
}

ShaderResourcePack::~ShaderResourcePack() {}

vpr::DescriptorSet* ShaderResourcePack::DescriptorSet(const char* group_name) noexcept {
    auto iter = rsrcGroupToIdxMap.find(group_name);
    if (iter != rsrcGroupToIdxMap.end()) {
        return descriptorSets[rsrcGroupToIdxMap.at(group_name)].get();
    }
    else {
        return nullptr;
    }
}

const vpr::DescriptorSet* ShaderResourcePack::DescriptorSet(const char* group_name) const noexcept {
    auto iter = rsrcGroupToIdxMap.find(group_name);
    if (iter != rsrcGroupToIdxMap.cend()) {
        return descriptorSets[rsrcGroupToIdxMap.at(group_name)].get();
    }
    else {
        return nullptr;
    }
}

vpr::DescriptorPool* ShaderResourcePack::DescriptorPool() noexcept {
    return descriptorPool.get();
}

const vpr::DescriptorPool* ShaderResourcePack::DescriptorPool() const noexcept {
    return descriptorPool.get();
}

std::vector<VkDescriptorSet> ShaderResourcePack::ShaderGroupSets(const std::string& shader_group_name) const noexcept {
    const auto& descriptor_indices = groupResourceUsages[shaderGroupNameIdxMap.at(shader_group_name)];
    std::vector<VkDescriptorSet> results;
    for (const size_t& idx : descriptor_indices) {
        results.emplace_back(descriptorSets[idx]->vkHandle());
    }
    return results;
}

void ShaderResourcePack::BindGroupSets(VkCommandBuffer cmd, const std::string& shader_group_name, const VkPipelineBindPoint bind_point) const {
    std::vector<VkDescriptorSet> sets_to_bind = ShaderGroupSets(shader_group_name);
    //vkCmdBindDescriptorSets(cmd, bind_point, )
}

VulkanResource* ShaderResourcePack::At(const std::string& group_name, const std::string& name) {
    return resources.at(group_name).at(name);
}

bool ShaderResourcePack::Has(const std::string& group_name, const std::string& name) const noexcept {
    if (resources.count(group_name) == 0) {
        return false;
    }
    else {
        if (resources.at(group_name).count(name) == 0) {
            return false;
        }
        else {
            return true;
        }
    }
}

VulkanResource* ShaderResourcePack::Find(const std::string& group_name, const std::string& name) noexcept {
    auto group_iter = resources.find(group_name);
    if (group_iter != std::cend(resources)) {
        auto rsrc_iter = group_iter->second.find(name);
        if (rsrc_iter != std::cend(group_iter->second)) {
            return rsrc_iter->second;
        }
        else {
            return nullptr;
        }
    }
    else {
        return nullptr;
    }
}

void ShaderResourcePack::createDescriptorPool() {
    auto& renderer = RenderingContext::Get();
    descriptorPool = std::make_unique<vpr::DescriptorPool>(renderer.Device()->vkHandle(), rsrcGroupToIdxMap.size());
    const auto& rsrc_counts = shaderPack->GetTotalDescriptorTypeCounts();
    descriptorPool->AddResourceType(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, rsrc_counts.UniformBuffers);
    descriptorPool->AddResourceType(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, rsrc_counts.UniformBuffersDynamic);
    descriptorPool->AddResourceType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, rsrc_counts.StorageBuffers);
    descriptorPool->AddResourceType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, rsrc_counts.StorageBuffersDynamic);
    descriptorPool->AddResourceType(VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, rsrc_counts.StorageTexelBuffers);
    descriptorPool->AddResourceType(VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, rsrc_counts.UniformTexelBuffers);
    descriptorPool->AddResourceType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, rsrc_counts.StorageImages);
    descriptorPool->AddResourceType(VK_DESCRIPTOR_TYPE_SAMPLER, rsrc_counts.Samplers);
    descriptorPool->AddResourceType(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, rsrc_counts.SampledImages);
    descriptorPool->AddResourceType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, rsrc_counts.CombinedImageSamplers);
    descriptorPool->Create();
}

void ShaderResourcePack::createSets() {

    setLayouts.resize(rsrcGroupToIdxMap.size());
    descriptorSets.resize(rsrcGroupToIdxMap.size());

    for (const auto& entry : rsrcGroupToIdxMap) {
        createSetResourcesAndLayout(entry.first);
    }
}

void ShaderResourcePack::createSetResourcesAndLayout(const std::string& name) {

    size_t num_resources = 0;
    const st::ResourceGroup* resource_group = shaderPack->GetResourceGroup(name.c_str());
    resource_group->GetResourcePtrs(&num_resources, nullptr);
    std::vector<const st::ShaderResource*> resources(num_resources);
    resource_group->GetResourcePtrs(&num_resources, resources.data());

    // Make set layout for all resources, even material
    createSetLayout(resources, name);

    {
        auto tags = resource_group->GetTags();
        std::vector<std::string> tag_strings;
        for (size_t i = 0; i < tags.NumStrings; ++i) {
            tag_strings.emplace_back(tags[i]);
        }
        
        if (auto iter = std::find(std::begin(tag_strings), std::end(tag_strings), "MaterialGroup"); iter != std::end(tag_strings)) {
            // If we find a material group, don't create meta-information for it.
            return;
        }
    }

    createResources(resources);
    createDescriptorSet(name);

}

void ShaderResourcePack::createSetLayout(const std::vector<const st::ShaderResource*>& resources, const std::string& name) {

    constexpr static VkDescriptorSetLayoutBinding base_layout_binding{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        VK_DESCRIPTOR_TYPE_MAX_ENUM,
        0,
        VK_SHADER_STAGE_ALL,
        nullptr
    };

    std::vector<VkDescriptorSetLayoutBinding> bindings(resources.size(), base_layout_binding);

    for (size_t i = 0; i < resources.size(); ++i) {
        bindings[i].descriptorCount = 1u;
        bindings[i].descriptorType = resources[i]->DescriptorType();
        bindings[i].binding = static_cast<uint32_t>(resources[i]->BindingIndex());
    }

    const size_t idx = rsrcGroupToIdxMap.at(name);
    setLayouts[idx] = std::make_unique<vpr::DescriptorSetLayout>(RenderingContext::Get().Device()->vkHandle());
    setLayouts[idx]->AddDescriptorBindings(static_cast<uint32_t>(bindings.size()), bindings.data());
    setLayouts[idx]->vkHandle();

}

void ShaderResourcePack::createDescriptorSet(const std::string& name) {
    auto& set_resources = resources.at(name);
    const size_t idx = rsrcGroupToIdxMap.at(name);

    descriptorSets[idx] = std::make_unique<vpr::DescriptorSet>(RenderingContext::Get().Device()->vkHandle());

    auto add_image_type = [&](const VulkanResource* rsrc) {
        VkDescriptorImageInfo image_info{
            VK_NULL_HANDLE,
            (VkImageView)rsrc->ViewHandle,
            VK_IMAGE_LAYOUT_UNDEFINED
        };

        const VkDescriptorType type = resourceTypesMap.at(rsrc);
        if (type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) {
            VulkanResource* sampler = reinterpret_cast<VulkanResource*>(rsrc->UserData);
            image_info.sampler = (VkSampler)sampler->Handle;
        }
        
        descriptorSets[idx]->AddDescriptorInfo(image_info, type, resourceBindingLocations.at(rsrc));
        
    };

    auto add_buffer_type = [&](const VulkanResource* rsrc) {
        const VkDescriptorBufferInfo buffer_info{
            (VkBuffer)rsrc->Handle,
            0,
            VK_WHOLE_SIZE
        };

        const VkDescriptorType type = resourceTypesMap.at(rsrc);
        const size_t binding_loc = resourceBindingLocations.at(rsrc);

        if (type == VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER || type == VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER) {
            descriptorSets[idx]->AddDescriptorInfo(buffer_info, (VkBufferView)rsrc->ViewHandle, type, binding_loc);
        }
        else {
            descriptorSets[idx]->AddDescriptorInfo(buffer_info, type, binding_loc);
        }

    };

    for (const auto& rsrc : set_resources) {
        switch (rsrc.second->Type) {
        case resource_type::IMAGE:
            add_image_type(rsrc.second);
            break;
        case resource_type::BUFFER:
            add_buffer_type(rsrc.second);
            break;
        case resource_type::SAMPLER:
            descriptorSets[idx]->AddSamplerBinding(resourceBindingLocations.at(rsrc.second));
            break;
        default:
            throw std::domain_error("Invalid resource.");
        }
    }

    descriptorSets[idx]->Init(descriptorPool->vkHandle(), setLayouts[idx]->vkHandle());
}

void ShaderResourcePack::getGroupNames() {
    auto names = shaderPack->GetResourceGroupNames();
    for (size_t i = 0; i < names.NumStrings; ++i) {
        rsrcGroupToIdxMap.emplace(names.Strings[i], i);
    }
}

void ShaderResourcePack::parseGroupBindingInfo() {
    
    std::vector<std::string> shader_group_strs;

    {
        auto shader_group_names = shaderPack->GetShaderGroupNames();
        for (size_t i = 0; i < shader_group_names.NumStrings; ++i) {
            shader_group_strs.emplace_back(shader_group_names[i]);
        }
    }

    groupResourceUsages.resize(shader_group_strs.size());
    shaderGroups.resize(shader_group_strs.size());
    pipelineLayouts.resize(shader_group_strs.size());

    for (const auto& str : shader_group_strs) {

        auto emplaced = shaderGroupNameIdxMap.emplace(str, shaderGroupNameIdxMap.size());
        const size_t idx = emplaced.first->second;
        shaderGroups[idx] = shaderPack->GetShaderGroup(str.c_str());

        std::vector<std::string> used_block_strs;
        {
            auto used_blocks = shaderGroups[idx]->GetUsedResourceBlocks();
            for (size_t i = 0; i < used_blocks.NumStrings; ++i) {
                if (auto iter = rsrcGroupToIdxMap.find(std::string(used_blocks[i])); iter != std::end(rsrcGroupToIdxMap)) {
                    groupResourceUsages[idx].emplace(iter->second);
                }
            }
        }

        createPipelineLayout(str);
    }

    // now have list of sets used by each resource group

}

void ShaderResourcePack::createPipelineLayout(const std::string & name) {
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

    std::vector<VkDescriptorSetLayout> set_layouts;
    auto& sets_used = groupResourceUsages.at(idx);
    for (const auto& set : sets_used) {
        set_layouts.emplace_back(setLayouts[set]->vkHandle());
    }

    pipelineLayouts[idx] = std::make_unique<vpr::PipelineLayout>(RenderingContext::Get().Device()->vkHandle());
    pipelineLayouts[idx]->Create(push_constant_ranges.size(), push_constant_ranges.data(), set_layouts.size(), set_layouts.data());

}

void ShaderResourcePack::createResources(const std::vector<const st::ShaderResource*>& resources) {
    for (const auto& rsrc : resources) {
        if (!Has(rsrc->ParentGroupName(), rsrc->Name())) {
            createResource(rsrc);
        }
    }
}

void ShaderResourcePack::createResource(const st::ShaderResource* rsrc) {
    switch (rsrc->DescriptorType()) {
    case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
        createTexelBuffer(rsrc, false);
        break;
    case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
        createTexelBuffer(rsrc, true);
        break;
    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
        createUniformBuffer(rsrc);
        break;
    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
        createStorageBuffer(rsrc);
        break;
    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
        createUniformBuffer(rsrc);
        break;
    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
        createStorageBuffer(rsrc);
        break;
    case VK_DESCRIPTOR_TYPE_SAMPLER:
        createSampler(rsrc);
        break;
    case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
        createSampledImage(rsrc);
        break;
    case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
        createCombinedImageSampler(rsrc);
        break;
    case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
        createSampledImage(rsrc);
        break;
    case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
        createCombinedImageSampler(rsrc);
        break;
    default:
        throw std::domain_error("ShaderResource had invalid type!");
    }
}

void ShaderResourcePack::createSampledImage(const st::ShaderResource* rsrc) {
    if (rsrc->FromFile()) {
        return;
    }

    auto& rsrc_context = ResourceContext::Get();
    const VkImageCreateInfo* image_info = &rsrc->ImageInfo();
    const VkImageViewCreateInfo* view_info = &rsrc->ImageViewInfo();

    auto emplaced = resources[rsrc->ParentGroupName()].emplace(rsrc->Name(), rsrc_context.CreateImage(image_info, view_info, 0, nullptr, memory_type::DEVICE_LOCAL, nullptr));
    if (!emplaced.second) {
        throw std::runtime_error("Failed to create sampler resource");
    }
    const VulkanResource* created = emplaced.first->second;
    resourceTypesMap.emplace(created, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
    resourceBindingLocations.emplace(emplaced.first->second, rsrc->BindingIndex());
    
}

void ShaderResourcePack::createTexelBuffer(const st::ShaderResource* texel_buffer, bool storage) {
    auto& group = resources[texel_buffer->ParentGroupName()];
    VkBufferCreateInfo buffer_info = vpr::vk_buffer_create_info_base;
    buffer_info.usage = storage ? VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT : VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT;
    buffer_info.size = texel_buffer->MemoryRequired();
    const VkBufferViewCreateInfo* view_info = &texel_buffer->BufferViewInfo();
    auto& rsrc_context = ResourceContext::Get();
    auto emplaced = group.emplace(texel_buffer->Name(), rsrc_context.CreateBuffer(&buffer_info, view_info, 0, nullptr, memory_type::DEVICE_LOCAL, nullptr));
    VkDescriptorType texel_subtype = storage ? VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER : VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    resourceTypesMap.emplace(emplaced.first->second, texel_subtype);
    resourceBindingLocations.emplace(emplaced.first->second, texel_buffer->BindingIndex());
}

void ShaderResourcePack::createUniformBuffer(const st::ShaderResource* uniform_buffer) {
    auto& group = resources[uniform_buffer->ParentGroupName()];
    VkBufferCreateInfo buffer_info = vpr::vk_buffer_create_info_base;
    buffer_info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    buffer_info.size = uniform_buffer->MemoryRequired();
    auto& rsrc_context = ResourceContext::Get();
    auto emplaced = group.emplace(uniform_buffer->Name(), rsrc_context.CreateBuffer(&buffer_info, nullptr, 0, nullptr, memory_type::HOST_VISIBLE_AND_COHERENT, nullptr));
    resourceTypesMap.emplace(emplaced.first->second, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    resourceBindingLocations.emplace(emplaced.first->second, uniform_buffer->BindingIndex());
}

void ShaderResourcePack::createStorageBuffer(const st::ShaderResource* storage_buffer) {
    auto& group = resources[storage_buffer->ParentGroupName()];
    VkBufferCreateInfo buffer_info = vpr::vk_buffer_create_info_base;
    buffer_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    buffer_info.size = storage_buffer->MemoryRequired();
    auto& rsrc_context = ResourceContext::Get();
    auto emplaced = group.emplace(storage_buffer->Name(), rsrc_context.CreateBuffer(&buffer_info, nullptr, 0, nullptr, memory_type::DEVICE_LOCAL, nullptr));
    resourceTypesMap.emplace(emplaced.first->second, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    resourceBindingLocations.emplace(emplaced.first->second, storage_buffer->BindingIndex());
}

void ShaderResourcePack::createCombinedImageSampler(const st::ShaderResource* rsrc) {

    auto& rsrc_context = ResourceContext::Get();
    const char* group_name = rsrc->ParentGroupName();
    const char* rsrc_name = rsrc->Name();
    const VkSamplerCreateInfo& sampler_info = rsrc->SamplerInfo();
    const VkImageCreateInfo& image_info = rsrc->ImageInfo();
    const VkImageViewCreateInfo& view_info = rsrc->ImageViewInfo();

    // create sampler first: "discard" it's pointer though, just trusting it's been created
    VulkanResource* sampler_rsrc = rsrc_context.CreateSampler(&sampler_info, nullptr);
    auto emplaced = resources[group_name].emplace(rsrc_name, rsrc_context.CreateImage(&image_info, &view_info, 0, nullptr, memory_type::DEVICE_LOCAL, sampler_rsrc));
    if (!emplaced.second) {
        throw std::runtime_error("Failed to create combined image sampler resource.");
    }
    resourceTypesMap.emplace(emplaced.first->second, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    resourceBindingLocations.emplace(emplaced.first->second, rsrc->BindingIndex());

}

void ShaderResourcePack::createSampler(const st::ShaderResource* rsrc) {
    const char* group_name = rsrc->ParentGroupName();
    const char* rsrc_name = rsrc->Name();
    auto& rsrc_context = ResourceContext::Get();
    auto emplaced = resources[group_name].emplace(rsrc_name, rsrc_context.CreateSampler(&rsrc->SamplerInfo(), nullptr));
    if (!emplaced.second) {
        throw std::runtime_error("Failed to create sampler resource.");
    }
    resourceTypesMap.emplace(emplaced.first->second, VK_DESCRIPTOR_TYPE_SAMPLER);
    resourceBindingLocations.emplace(emplaced.first->second, rsrc->BindingIndex());
}
