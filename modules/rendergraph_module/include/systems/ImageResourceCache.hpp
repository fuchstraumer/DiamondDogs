#pragma once
#ifndef VPSK_IMAGE_RESOURCE_CACHE_HPP
#define VPSK_IMAGE_RESOURCE_CACHE_HPP
#include "ForwardDecl.hpp"
#include <vulkan/vulkan.h>
#include <unordered_map>
#include <string>
#include <memory>

namespace st {
    class ResourceUsage;
    class ShaderResource;
    class ShaderGroup;
}

namespace vpsk {

    class Texture;

    class ImageResourceCache {
        ImageResourceCache(const ImageResourceCache&) = delete;
        ImageResourceCache& operator=(const ImageResourceCache&) = delete;
    public:

        ImageResourceCache(const vpr::Device* dvc);
        ~ImageResourceCache();

        //vpr::Image* CreateImage(const std::string& name, const VkImageCreateInfo& img_info, const VkImageViewCreateInfo& view_info);
        //vpr::Sampler* CreateSampler(const std::string& name, const VkSamplerCreateInfo& sampler_info);

        void AddResources(const std::vector<const st::ShaderResource*>& resources);
        void AddResource(const st::ShaderResource* rsrc);

        vpr::Image* FindImage(const std::string& group_name, const std::string & rsrc_name);
        vpr::Sampler* FindSampler(const std::string& group_name, const std::string & rsrc_name);
        vpr::Image* Image(const std::string& group_name, const std::string & rsrc_name);
        vpr::Sampler* Sampler(const std::string& group_name, const std::string & rsrc_name);

        bool HasImage(const std::string& group_name, const std::string & rsrc_name) const noexcept;
        bool HasSampler(const std::string& group_name, const std::string & rsrc_name) const noexcept;
        
    private:

        void createResource(const st::ShaderResource* rsrc);
        void createCombinedImageSampler(const st::ShaderResource* rsrc);
        void createSampler(const st::ShaderResource* rsrc);
        void createSampledImage(const st::ShaderResource* rsrc);

        const vpr::Device* device;
        std::unordered_map<std::string, std::unordered_map<std::string, std::unique_ptr<vpr::Sampler>>> samplers;
        std::unordered_map<std::string, std::unordered_map<std::string, std::unique_ptr<vpr::Image>>> images;

    };

}

#endif //!VPSK_IMAGE_RESOURCE_CACHE_HPP
