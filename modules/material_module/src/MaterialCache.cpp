#include "MaterialCache.hpp"
#include "RenderingContext.hpp"
#include "ResourceContext.hpp"
#include "ResourceLoader.hpp"
#include "LogicalDevice.hpp"
#include "DescriptorTemplate.hpp"
#include "utility/MurmurHash.hpp"
#include "EmbeddedTextures.hpp"
#include "MaterialStructures.hpp"
#pragma warning(push, 1) // silence warnings from other libs
#include "easylogging++.h"
#include <mango/core/memory.hpp>
#include <mango/image/image.hpp>
#pragma warning(pop)
#include <algorithm>
#include <filesystem>
#include <array>
#include <unordered_map>
#include <atomic>
#include <shared_mutex>
#include <set>
#include <string>

// will return to these eventually to change error texture displayed based on type of error
// each one will be a slightly different image, and there will additionally be another image
// representing a texture that's still just loading
enum class texture_failure_type : uint8_t
{
    InvalidPath = 0,
    IncorrectBindlessIndex = 1,
    InvalidFormat = 2,
    InvalidConversion = 3,
};

struct image_loaded_callback_data_t
{
    uint64_t mtlHash;
    texture_type type;
    constexpr bool operator==(const image_loaded_callback_data_t& other) const noexcept;
    constexpr bool operator<(const image_loaded_callback_data_t& other) const noexcept;
};

struct MaterialGroupInstance
{
    // Instance of resources that will be bound to a table of bindless resources

    // The resource used for the array of bindless indices
    VulkanResource* indexSSBO;
    // Resource for array of material parameters
    VulkanResource* parameterSSBO;
    // Array of textures used with this material
    std::vector<VulkanResource*> textures;
};

namespace
{

    uint64_t HashMaterialCreateInfo(const MaterialCreateInfo& createInfo);
    VulkanResource* createUBO(const char* name, const material_parameters_t& params);
    void* loadImageDataFn(const char* fname, void* user_data);
    void destroyImageDataFn(void* object, void* user_data);

}

namespace
{
    uint64_t HashMaterialCreateInfo(const MaterialCreateInfo& createInfo)
    {
        // Params hash hashes contents as well: difference in values will be invalid
        /*const uint64_t paramsHash = MurmurHash2(&createInfo.Parameters, sizeof(material_parameters_t), 0xFD);
        std::string concatenatedNames{ createInfo };
        concatenatedNames.reserve(1024);
        for (size_t i = 0; i < createInfo.NumTextures; ++i)
        {
            concatenatedNames += std::string(createInfo.TexturePaths[i]);
        }
        concatenatedNames.shrink_to_fit();
        const uint64_t namesHash = MurmurHash2((void*)concatenatedNames.c_str(), sizeof(char) * concatenatedNames.length(), 0xFC);
        return namesHash ^ paramsHash;*/
        return 0;
    }

    VulkanResource* createUBO(const char* name, const material_parameters_t& params)
    {
        auto& rsrc_context = ResourceContext::Get();

        constexpr static VkBufferCreateInfo ubo_create_info
        {
            VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            nullptr,
            0,
            sizeof(material_parameters_t),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_SHARING_MODE_EXCLUSIVE, // safe because we only write to it from CPU
            0u,
            nullptr
        };

        const gpu_resource_data_t ubo_data
        {
            &params, sizeof(material_parameters_t), 0u, VK_QUEUE_FAMILY_IGNORED
        };

        constexpr static resource_creation_flags uboFlags{ ResourceCreateMemoryStrategyMinFragmentation | ResourceCreateUserDataAsString };
        VulkanResource* result = nullptr;
        result = rsrc_context.CreateBuffer(&ubo_create_info, nullptr, 1u, &ubo_data, resource_usage::CPU_ONLY, uboFlags, (void*)name);
        assert(result);

        return result;
    }

    void* loadImageDataFn(const char* fname, void* user_data)
    {
        using namespace mango;
        auto& rsrc_context = ResourceContext::Get();
        const auto* device = RenderingContext::Get().Device();
        const uint32_t graphics_queue_idx = device->QueueFamilyIndices().Graphics;

        static const std::map<mango::Format, VkComponentMapping> formatComponentMappings
        {
            { FORMAT_A8, VkComponentMapping{ VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_R } },
            { FORMAT_L8, VkComponentMapping{ VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_R } },
            { FORMAT_R16, VkComponentMapping{ VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_R } },
            { FORMAT_L16, VkComponentMapping{ VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_R } },
            { FORMAT_L8A8, VkComponentMapping{ VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_ZERO, VK_COMPONENT_SWIZZLE_ZERO } },
            { FORMAT_R8G8B8A8, VkComponentMapping{ VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A } },
            { FORMAT_R8G8B8X8, VkComponentMapping{ VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_ZERO }},
            { FORMAT_B8G8R8, VkComponentMapping{ VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_ZERO }},
            { FORMAT_B8G8R8X8, VkComponentMapping{ VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_ZERO }},
            { FORMAT_B8G8R8A8, VkComponentMapping{ VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A }}
        };

        loaded_texture_data_t* result = new loaded_texture_data_t(fname);

        auto iter = formatComponentMappings.find(result->bitmap.format);
        if (iter == std::cend(formatComponentMappings))
        {
            LOG(ERROR) << "Couldn't find valid component mapping for imported file format!";
            throw std::runtime_error("Bad image format loaded.");
        }

        const VkComponentMapping view_mapping = iter->second;

        if (result->bitmap.format == FORMAT_R8G8B8 || result->bitmap.format == FORMAT_B8G8R8A8 || result->bitmap.format == FORMAT_B8G8R8 ||
            result->bitmap.format == FORMAT_R8G8B8X8 || result->bitmap.format == FORMAT_B8G8R8X8)
        {
            Bitmap dest_blit(result->bitmap.width, result->bitmap.height, FORMAT_R8G8B8A8);
            dest_blit.blit(0, 0, result->bitmap);
            result->bitmap = std::move(dest_blit);
        }

        static const std::map<mango::Format, VkFormat> formatConversionMap{
            { FORMAT_A8, VK_FORMAT_R8_UNORM },
            { FORMAT_L8, VK_FORMAT_R8_UNORM },
            { FORMAT_R16, VK_FORMAT_R16_UNORM },
            { FORMAT_L16, VK_FORMAT_R16_UNORM },
            { FORMAT_L8A8, VK_FORMAT_R8G8_UNORM },
            { FORMAT_R8G8B8A8, VK_FORMAT_R8G8B8A8_UNORM }
        };

        VkFormat found_format = VK_FORMAT_UNDEFINED;

        for (const auto& fmt : formatConversionMap)
        {
            if (result->bitmap.format == fmt.first)
            {
                found_format = fmt.second;
                break;
            }
        }

        if (found_format == VK_FORMAT_UNDEFINED)
        {
            LOG(ERROR) << "Invalid format from Mango";
            LOG(ERROR) << "File Path: " << fname;
            throw std::domain_error("Invalid format for image!");
        }

        const VkFormat image_format = found_format;

        const size_t img_footprint = result->bitmap.format.bytes() * result->bitmap.width * result->bitmap.height;
        const gpu_image_resource_data_t img_rsrc_data{
            result->bitmap.address(),
            img_footprint,
            size_t(result->bitmap.width),
            size_t(result->bitmap.height),
            0u,
            1u,
            0u,
            graphics_queue_idx
        };

        const uint32_t transfer_idx = device->QueueFamilyIndices().Transfer;
        const uint32_t graphics_idx = device->QueueFamilyIndices().Graphics;
        const VkSharingMode sharing_mode = (transfer_idx != graphics_idx) ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE;
        const std::array<uint32_t, 2> queue_family_indices{ graphics_idx, transfer_idx };

        const VkImageCreateInfo img_create_info
        {
            VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            nullptr,
            0,
            VK_IMAGE_TYPE_2D,
            image_format,
            VkExtent3D{ uint32_t(result->bitmap.width), uint32_t(result->bitmap.height), 1u },
            1u,
            1u,
            VK_SAMPLE_COUNT_1_BIT,
            device->GetFormatTiling(image_format, VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT),
            VK_IMAGE_USAGE_SAMPLED_BIT,
            sharing_mode,
            sharing_mode == VK_SHARING_MODE_CONCURRENT ? static_cast<uint32_t>(queue_family_indices.size()) : 0u,
            sharing_mode == VK_SHARING_MODE_CONCURRENT ? queue_family_indices.data() : nullptr,
            VK_IMAGE_LAYOUT_UNDEFINED
        };

        const VkImageViewCreateInfo view_create_info
        {
            VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            nullptr,
            0,
            VK_NULL_HANDLE,
            VK_IMAGE_VIEW_TYPE_2D,
            image_format,
            view_mapping,
            VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
        };

        const std::string texture_name_str = std::filesystem::path(fname).filename().string();
        result->resource = rsrc_context.CreateImage(&img_create_info, &view_create_info, 1, &img_rsrc_data, resource_usage::GPU_ONLY, ResourceCreateMemoryStrategyMinMemory | ResourceCreateUserDataAsString, (void*)texture_name_str.c_str());

        return result;
    }

    void destroyImageDataFn(void* object, void* user_data)
    {
        // destroys loaded image data (releasing some RAM), but doesn't destroy Vulkan resource since it likely still is in use.
        // we manage that lifetime separately.
        loaded_texture_data_t* pointer = reinterpret_cast<loaded_texture_data_t*>(object);
        delete pointer;
    }

}