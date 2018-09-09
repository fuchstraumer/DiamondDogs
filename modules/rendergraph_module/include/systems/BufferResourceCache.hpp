#pragma once
#ifndef VPSK_BUFFER_RESOURCE_CACHE_HPP
#define VPSK_BUFFER_RESOURCE_CACHE_HPP
#include "ForwardDecl.hpp"
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>

namespace st {
    class ShaderGroup;
    class ShaderPack;
    class ShaderResource;
}

namespace vpsk {

    /** Represents resources that won't be loaded into or read from by the host. Shader-only buffer resources, effectively.
     *  Thus they exist a bit outside of our usual loader system.
    */
    class BufferResourceCache {
        BufferResourceCache(const BufferResourceCache&) = delete;
        BufferResourceCache& operator=(const BufferResourceCache&) = delete;
    public:

        BufferResourceCache(const vpr::Device* dvc);
        ~BufferResourceCache();
        void AddResources(const std::vector<const st::ShaderResource*>& resources);
        void AddResource(const st::ShaderResource* resource);
        vpr::Buffer* at(const std::string& group_name, const std::string& name);
        vpr::Buffer* find(const std::string& group_name, const std::string& name) noexcept;
        bool HasResource(const std::string& group_name, const std::string& name) const noexcept;

    private:
        void createTexelBuffer(const st::ShaderResource * texel_buffer, bool storage);
        void createUniformBuffer(const st::ShaderResource * uniform_buffer);
        void createStorageBuffer(const st::ShaderResource * storage_buffer);
        void createResources(const std::vector<const st::ShaderResource*>& resources);
        void createResource(const st::ShaderResource * rsrc);
        const vpr::Device* device;
        std::unordered_map<std::string, std::unordered_map<std::string, std::unique_ptr<vpr::Buffer>>> buffers;
    };

}

#endif //!VPSK_BUFFER_RESOURCE_CACHE_HPP
