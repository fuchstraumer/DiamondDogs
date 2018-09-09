#pragma once
#ifndef VPSK_SHADER_RESOURCE_PACK_HPP
#define VPSK_SHADER_RESOURCE_PACK_HPP
#include "ForwardDecl.hpp"
#include <unordered_map>
#include <vector>
#include <memory>
#include <set>
#include <vulkan/vulkan.h>

namespace st {
    class ShaderPack;
    class ShaderResource;
    class ResourceUsage;
}

namespace vpsk {

    class RenderGraph;

    class ShaderResourcePack {
        ShaderResourcePack(const ShaderResourcePack&) = delete;
        ShaderResourcePack& operator=(const ShaderResourcePack&) = delete;
    public:

        // ShaderPacks aren't owned/loaded by this object: they are cached/stored elsewhere
        ShaderResourcePack(RenderGraph& _graph, const st::ShaderPack* pack);
        ShaderResourcePack(ShaderResourcePack&& other) noexcept;
        ShaderResourcePack& operator=(ShaderResourcePack&& other) noexcept;
        ~ShaderResourcePack();

        vpr::DescriptorSet* DescriptorSet(const char* rsrc_group_name) noexcept;
        const vpr::DescriptorSet* DescriptorSet(const char* rsrc_group_name) const noexcept;
        vpr::DescriptorPool* DescriptorPool() noexcept;
        const vpr::DescriptorPool* DescriptorPool() const noexcept;
        std::vector<VkDescriptorSet> ShaderGroupSets(const char* shader_group_name) const noexcept;

    private:

        void createResources(const std::vector<const st::ShaderResource*>& resources);
        void createDescriptorPool();
        void createSingleSet(const std::string& name);
        void createSets();

        void getGroupNames();

        RenderGraph& graph;
        std::unique_ptr<vpr::DescriptorPool> descriptorPool;
        std::unordered_map<std::string, size_t> rsrcGroupToIdxMap;
        std::unordered_map<std::string, std::set<size_t>> shaderGroupSetIndices;
        std::vector<std::unique_ptr<vpr::DescriptorSet>> descriptorSets;
        std::vector<std::unique_ptr<vpr::DescriptorSetLayout>> setLayouts;
        const st::ShaderPack* shaderPack;
    };

}

#endif //!VPSK_SHADER_RESOURCE_PACK_HPP
