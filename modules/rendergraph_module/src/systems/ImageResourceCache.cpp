#include "systems/ImageResourceCache.hpp"
#include "resource/Image.hpp"
#include "resource/Sampler.hpp"
#include "resources/Texture.hpp"
#include "core/ShaderResource.hpp"
namespace vpsk {

    ImageResourceCache::ImageResourceCache(const vpr::Device * dvc) : device(dvc) {}

    ImageResourceCache::~ImageResourceCache() { }

    void ImageResourceCache::AddResources(const std::vector<const st::ShaderResource*>& resources) {
        for (const auto& rsrc : resources) {
            createResource(rsrc);
        }
    }

    void ImageResourceCache::AddResource(const st::ShaderResource * rsrc) {
        createResource(rsrc);
    }

    vpr::Image* ImageResourceCache::FindImage(const std::string& group_name, const std::string & rsrc_name) {
        auto group_iter = images.find(group_name);
        if (group_iter == images.end()) {
            return nullptr;
        }
        else {
            auto rsrc_iter = group_iter->second.find(rsrc_name);
            if (rsrc_iter == group_iter->second.end()) {
                return nullptr;
            }
            else {
                return rsrc_iter->second.get();
            }
        }
    }

    vpr::Sampler* ImageResourceCache::FindSampler(const std::string& group_name, const std::string & rsrc_name) {
        auto group_iter = samplers.find(group_name);
        if (group_iter == samplers.end()) {
            return nullptr;
        }
        else {
            auto rsrc_iter = group_iter->second.find(rsrc_name);
            if (rsrc_iter == group_iter->second.end()) {
                return nullptr;
            }
            else {
                return rsrc_iter->second.get();
            }
        }
    }

    vpr::Image* ImageResourceCache::Image(const std::string& group_name, const std::string & rsrc_name) {
        return images.at(group_name).at(rsrc_name).get();
    }

    vpr::Sampler* ImageResourceCache::Sampler(const std::string& group_name, const std::string & rsrc_name) {
        return samplers.at(group_name).at(rsrc_name).get();
    }

    bool ImageResourceCache::HasImage(const std::string & group_name, const std::string & rsrc_name) const noexcept {
        if (images.count(group_name) == 0) {
            return false;
        }
        else {
            if (images.at(group_name).count(rsrc_name) != 0) {
                return true;
            }
            else {
                return false;
            }
        }
    }

    bool ImageResourceCache::HasSampler(const std::string & group_name, const std::string & rsrc_name) const noexcept {
        if (samplers.count(group_name) == 0) {
            return false;
        }
        else {
            if (samplers.at(group_name).count(rsrc_name) != 0) {
                return true;
            }
            else {
                return false;
            }
        }
    }

    void ImageResourceCache::createResource(const st::ShaderResource * rsrc) {
        switch (rsrc->DescriptorType()) {
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
            // Probably just a resource type we don't care about.
            break;
        }
    }

    void ImageResourceCache::createCombinedImageSampler(const st::ShaderResource * rsrc) {
        createSampler(rsrc);
        createSampledImage(rsrc);
    }

    void ImageResourceCache::createSampler(const st::ShaderResource * rsrc) {
        const char* group_name = rsrc->ParentGroupName();
        const char* rsrc_name = rsrc->Name();
        if (!HasSampler(group_name, rsrc_name)) {
            auto emplaced = samplers[group_name].emplace(rsrc_name, std::make_unique<vpr::Sampler>(device, rsrc->SamplerInfo()));
            if (!emplaced.second) {
                throw std::runtime_error("Failed to create sampler resource.");
            }
        }   
    }

    void ImageResourceCache::createSampledImage(const st::ShaderResource * rsrc) {
        if (rsrc->FromFile()) {
            return;
        }

        const char* group_name = rsrc->ParentGroupName();
        const char* rsrc_name = rsrc->Name();
        
        if (!HasImage(group_name, rsrc_name)) {
            auto emplaced = images[group_name].emplace(rsrc_name, std::make_unique<vpr::Image>(device));
            if (!emplaced.second) {
                throw std::runtime_error("Failed to create sampler resource");
            }
            emplaced.first->second->Create(rsrc->ImageInfo());
            emplaced.first->second->CreateView(rsrc->ImageViewInfo());
        }
    }

}
