#pragma once
#ifndef DIAMOND_DOGS_SHADER_RESOURCE_CACHE_HPP
#define DIAMOND_DOGS_SHADER_RESOURCE_CACHE_HPP
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>

namespace st {
    class ShaderGroup;
    class ShaderPack;
    class ShaderResource;
}

struct VulkanResource;

/*
    Creates resources using shadertools objects, and keeps the handles
    grouped together based on their usages in shaders (by resource groups)

    Will be created on a per-shader basis, usually. Resources, once created,
    are effectively immutable.
*/

class ShaderResourceCache {
    ShaderResourceCache(const ShaderResourceCache&) = delete;
    ShaderResourceCache& operator=(const ShaderResourceCache&) = delete;
public:

    ShaderResourceCache(std::string shader_name);
    ~ShaderResourceCache();
    
    const std::string& Name() const noexcept;
    void AddResources(const std::vector<const st::ShaderResource*>& resources);
    void AddResource(const st::ShaderResource* resource);
    const VulkanResource* At(const std::string& group_name, const std::string& name) const;
    const VulkanResource* Find(const std::string& group_name, const std::string& name) const noexcept;
    bool Has(const std::string& group_name, const std::string& name) const noexcept;

private:

    void createResources(const std::vector<const st::ShaderResource*>& resources);
    void createResource(const st::ShaderResource * rsrc);
    void createSampledImage(const st::ShaderResource* rsrc);
    void createTexelBuffer(const st::ShaderResource * texel_buffer, bool storage);
    void createUniformBuffer(const st::ShaderResource * uniform_buffer);
    void createStorageBuffer(const st::ShaderResource * storage_buffer);
    void createCombinedImageSampler(const st::ShaderResource* rsrc);
    void createSampler(const st::ShaderResource* rsrc);

    std::string shaderName;
    std::unordered_map<std::string, std::unordered_map<std::string, VulkanResource*>> resources;

};


#endif //!DIAMOND_DOGS_SHADER_RESOURCE_CACHE_HPP
