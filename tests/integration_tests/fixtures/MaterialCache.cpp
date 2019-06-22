#include "MaterialCache.hpp"
#include "RenderingContext.hpp"
#include "ResourceContext.hpp"
#include "ResourceLoader.hpp"
#include "LogicalDevice.hpp"
#include "MurmurHash.hpp"
#pragma warning(push, 1) // silence warnings from other libs
#include "tinyobj_loader_opt.h"
#include "easylogging++.h"
#include <mango/core/memory.hpp>
#include <mango/image/image.hpp>
#pragma warning(pop)
#include <algorithm>
#include <filesystem>
#ifdef _MSC_VER
#include <execution>
#endif
#include <shared_mutex>

namespace
{
    /*
        Store these for later so we can unload things from RAM
        once they've been used to make a texture and that texture
        data has been uploaded to the GPU
    */
    struct resource_loader_data_t
    {
        // we check to see if this is in the upload queue: if it is, do nothing. if not, unload.
        VulkanResource* CreatedResource{ nullptr };
        std::string FileType;
        std::string FilePath;
    };
    static std::vector<resource_loader_data_t> loadedImages;
    // used for various static functionalities
    static std::mutex staticMaterialMutex;
    static std::shared_mutex materialSharedMutex;

    template<texture_type tex_type_e>
    constexpr static texture_type TEXTURE_TYPE_VAL = tex_type_e;
    // force initialization now because we use these by address, not value, so we need a fixed address
    constexpr static auto AMBIENT_TEXTURE_TYPE = TEXTURE_TYPE_VAL<texture_type::Ambient>;
    constexpr static auto DIFFUSE_TEXTURE_TYPE = TEXTURE_TYPE_VAL<texture_type::Diffuse>;
    constexpr static auto SPECULAR_TEXTURE_TYPE = TEXTURE_TYPE_VAL<texture_type::Specular>;
    constexpr static auto BUMP_TEXTURE_TYPE = TEXTURE_TYPE_VAL<texture_type::Bump>;
    constexpr static auto DISPLACEMENT_TEXTURE_TYPE = TEXTURE_TYPE_VAL<texture_type::Displacement>;
    constexpr static auto ALPHA_TEXTURE_TYPE = TEXTURE_TYPE_VAL<texture_type::Alpha>;
    constexpr static auto ROUGHNESS_TEXTURE_TYPE = TEXTURE_TYPE_VAL<texture_type::Roughness>;
    constexpr static auto METALLIC_TEXTURE_TYPE = TEXTURE_TYPE_VAL<texture_type::Metallic>;
    constexpr static auto EMISSIVE_TEXTURE_TYPE = TEXTURE_TYPE_VAL<texture_type::Emissive>;
    constexpr static auto NORMAL_TEXTURE_TYPE = TEXTURE_TYPE_VAL<texture_type::Normal>;

    struct loaded_texture_data_t
    {
        loaded_texture_data_t(const char* fname) : bitmap(fname) {}
        mango::Bitmap bitmap;
        VulkanResource* resource{ nullptr };
    }; 
    
    enum class lock_mode : uint8_t
    {
        Invalid = 0,
        Read = 1,
        Write = 2,
        Released = 3
    };

    struct rw_lock_guard
    {

        rw_lock_guard(lock_mode _mode, std::shared_mutex& _mut) noexcept;
        ~rw_lock_guard() noexcept;

        void release_lock();
        void upgrade_to_write_mode();
        void downgrade_to_read_mode();

        rw_lock_guard(rw_lock_guard&& other) noexcept : mut(std::move(other.mut)), mode(std::move(other.mode)) {}
        rw_lock_guard& operator=(rw_lock_guard&& other) = delete;
        rw_lock_guard(const rw_lock_guard&) = delete;
        rw_lock_guard& operator=(const rw_lock_guard&) = delete;

    private:
        lock_mode mode{ lock_mode::Invalid };
        std::shared_mutex& mut;
    };

    float extract_valid_material_param(const float input);
    glm::vec3 extract_valid_material_param(const glm::vec3& in) noexcept;
    material_parameters_t from_tinyobj_material(const tinyobj_opt::material_t& mtl) noexcept;
    VulkanResource* createUBO(const char* name, const material_parameters_t& params); 
    std::string FindDebugTexture();
    void* loadImageDataFn(const char* fname, void* user_data);
    void destroyImageDataFn(void* object, void* user_data);
    void textureLoadedCallback(void* instance, void* loaded_data, void* user_data);

}

// used when a material explicitly fails to load. classic pink/black squares.
static VulkanResource* failedLoadDebugTexture{ nullptr };

MaterialCache::material_cache_page_t::material_cache_page_t() noexcept
{
    // filling with this value so that we reserve some memory for each entry ahead of time
    namesArray.fill(std::string{ "unused_material_name" });
    parameterUBOs.fill(nullptr);
    albedoMaps.fill(nullptr);
    alphaMaps.fill(nullptr);
    specularMaps.fill(nullptr);
    bumpMaps.fill(nullptr);
    displacementMaps.fill(nullptr);
    normalMaps.fill(nullptr);
    aoMaps.fill(nullptr);
    metallicMaps.fill(nullptr);
    roughnessMaps.fill(nullptr);
    emissiveMaps.fill(nullptr);
}

MaterialCache& MaterialCache::Get() noexcept
{
    static MaterialCache pool;
    return pool;
}

void MaterialCache::ReleaseCpuData()
{
    auto& rsrc_context = ResourceContext::Get();
    auto& rsrc_loader = ResourceLoader::GetResourceLoader();
    std::lock_guard imagesGuard{ staticMaterialMutex };

    while (!loadedImages.empty())
    {
        auto& loaded_img_data = loadedImages.back();

        if (!rsrc_context.ResourceInTransferQueue(loaded_img_data.CreatedResource))
        {
            rsrc_loader.Unload(loaded_img_data.FileType.c_str(), loaded_img_data.FilePath.c_str());
        }

        loadedImages.pop_back();
    }

}

MaterialInstance* MaterialCache::LoadTinyobjMaterial(const void* mtlPtr, const char* material_dir)
{
    auto& resource_loader = ResourceLoader::GetResourceLoader();
    resource_loader.Subscribe("MANGO", loadImageDataFn, destroyImageDataFn);

    assert(mtlPtr != nullptr);
    const auto& mtl = *reinterpret_cast<const tinyobj_opt::material_t*>(mtlPtr);

    material_parameters_t params = from_tinyobj_material(mtl);
    const uint64_t paramsHash = MurmurHash2(&params, sizeof(params), 0xFD);

    std::string concatenatedNames{ mtl.name };
    concatenatedNames.reserve(1024);
    concatenatedNames += mtl.ambient_texname;
    concatenatedNames += mtl.diffuse_texname;
    concatenatedNames += mtl.specular_texname;
    concatenatedNames += mtl.specular_highlight_texname;
    concatenatedNames += mtl.bump_texname;
    concatenatedNames += mtl.displacement_texname;
    concatenatedNames += mtl.alpha_texname;
    concatenatedNames += mtl.roughness_texname;
    concatenatedNames += mtl.metallic_texname;
    concatenatedNames += mtl.sheen_texname;
    concatenatedNames += mtl.emissive_texname;
    concatenatedNames += mtl.normal_texname;

    const uint64_t namesHash = MurmurHash2((void*)concatenatedNames.c_str(), sizeof(char) * concatenatedNames.length(), 0xFC);
    const uint64_t materialHash = paramsHash ^ namesHash;

    auto& currMtlPage = materialPages[currentPage];

    rw_lock_guard dataGuard(lock_mode::Read, materialSharedMutex);

    if (auto iter = currMtlPage.hashToIndicesMap.find(materialHash); iter != std::end(currMtlPage.hashToIndicesMap))
    {
        // material already exists
        const uint32_t materialIdx = iter->second;
        // data we use from here is all in arrays that won't suffer from iterator invalidation: release lock
        dataGuard.release_lock();

    }
    else
    {
        
        const uint32_t materialIdx = currMtlPage.currTextureCount;
        currMtlPage.currTextureCount += 1;


        // material doesn't exist, we have to load it. upgrade to write so we can emplace new hash
        dataGuard.upgrade_to_write_mode();
        auto emplace_iter = currMtlPage.hashToIndicesMap.emplace(materialHash, materialIdx);
        
        dataGuard.release_lock();

        currMtlPage.parametersArray[materialIdx] = std::move(params);
        // this is quick enough that it's not worth running through the loader
        const uint32_t uboIdx = currMtlPage.containerIndices.parametersCpuIdx.fetch_add(1u);
        currMtlPage.parameterUBOs[materialIdx] = createUBO(mtl.name.c_str(), currMtlPage.parametersArray[materialIdx]);
        // index for UBO is gonna be unique each time, since every material has a UBO
        currMtlPage.indicesArray[materialIdx].ParamsIdx = materialIdx;

        // now the tricky part

    }
    
    return nullptr;
}

void MaterialCache::textureLoadedCallback(void* loaded_image, void* user_data)
{
    loaded_texture_data_t* textureData = reinterpret_cast<loaded_texture_data_t*>(loaded_image);
    image_loaded_callback_data_t* callbackData = reinterpret_cast<image_loaded_callback_data_t*>(user_data);

}

namespace
{

    rw_lock_guard::rw_lock_guard(lock_mode _mode, std::shared_mutex& _mutex) noexcept : mut(_mutex), mode(_mode)
    {
        if (mode == lock_mode::Read)
        {
            mut.lock_shared();
        }
        else
        {
            mut.lock();
        }
    }

    rw_lock_guard::~rw_lock_guard() noexcept
    {
        if (mode == lock_mode::Read)
        {
            mut.unlock_shared();
        }
        else if (mode == lock_mode::Write)
        {
            mut.unlock();
        }
    }

    void rw_lock_guard::upgrade_to_write_mode()
    {
        assert(mode == lock_mode::Read);
        mode = lock_mode::Write;
        mut.unlock_shared();
        mut.lock();
    }

    void rw_lock_guard::downgrade_to_read_mode()
    {
        assert(mode == lock_mode::Write);
        mode = lock_mode::Read;
        mut.unlock();
        mut.lock_shared();
    }

    void rw_lock_guard::release_lock()
    {
        assert(mode != lock_mode::Released);

        if (mode == lock_mode::Read)
        {
            mut.unlock_shared();
        }
        else
        {
            mut.unlock();
        }

        mode = lock_mode::Released;
    }

    float extract_valid_material_param(const float input)
    {
        // the various structures we read can end up with values that are weird:
        // this doesn't work great on the GPU, since we can send it really tiny
        // values (not worth computing, we should zero them) or really large
        // values (blatantly broken). so we filter those out here.

        if (std::isnan(input))
        {
            return 0.0f;
        }

        if (input < -1.0e6f || input > 1.0e6f)
        {
            return 0.0f;
        }

        if (std::abs(input) < 1.0e-4f)
        {
            return 0.0f;
        }

        return input;
    }

    glm::vec3 extract_valid_material_param(const glm::vec3& in) noexcept
    {
        return glm::vec3{
            extract_valid_material_param(in.x),
            extract_valid_material_param(in.y),
            extract_valid_material_param(in.z)
        };
    }

    material_parameters_t from_tinyobj_material(const tinyobj_opt::material_t& mtl) noexcept
    {
        material_parameters_t parameters;
        parameters.ambient = extract_valid_material_param(glm::vec3(mtl.ambient[0], mtl.ambient[1], mtl.ambient[2]));
        parameters.diffuse = extract_valid_material_param(glm::vec3(mtl.diffuse[0], mtl.diffuse[1], mtl.diffuse[2]));
        parameters.specular = extract_valid_material_param(glm::vec3(mtl.specular[0], mtl.specular[1], mtl.specular[2]));
        parameters.transmittance = extract_valid_material_param(glm::vec3(mtl.transmittance[0], mtl.transmittance[1], mtl.transmittance[2]));
        parameters.emission = extract_valid_material_param(glm::vec3(mtl.emission[0], mtl.emission[1], mtl.emission[2]));
        parameters.shininess = extract_valid_material_param(mtl.shininess);
        parameters.ior = extract_valid_material_param(mtl.ior);
        parameters.alpha = extract_valid_material_param(mtl.dissolve);
        parameters.illum = static_cast<int32_t>(mtl.illum);
        parameters.roughness = extract_valid_material_param(mtl.roughness);
        parameters.metallic = extract_valid_material_param(mtl.metallic);
        parameters.sheen = extract_valid_material_param(mtl.sheen);
        parameters.clearcoat_thickness = extract_valid_material_param(mtl.clearcoat_thickness);
        parameters.clearcoat_roughness = extract_valid_material_param(mtl.clearcoat_roughness);
        parameters.anisotropy = extract_valid_material_param(mtl.anisotropy);
        parameters.anisotropy_rotation = extract_valid_material_param(mtl.anisotropy_rotation);
        LOG_IF(!mtl.unknown_parameter.empty(), WARNING) << "tinyobj material " << mtl.name << " has unknown parameters that will be discarded.";
        return parameters;
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

    std::string FindDebugTexture()
    {
        namespace stdfs = std::experimental::filesystem;
        static const std::string TextureFname("FailedTexLoad.png");

        auto case_insensitive_comparison = [](const std::string& fname, const std::string& curr_entry)->bool
        {
#ifdef _MSC_VER
            return std::equal(std::execution::par_unseq, fname.cbegin(), fname.cend(), curr_entry.cbegin(), curr_entry.cend(), [](const char a, const char b)
                {
                    return std::tolower(a) == std::tolower(b);
                });
#else // afaik only MSVC supports <execution>
            return std::equal(fname.cbegin(), fname.cend(), curr_entry.cbegin(), curr_entry.cend(), [](const char a, const char b)
                {
                    return std::tolower(a) == std::tolower(b);
                });
#endif
        };

        stdfs::path starting_path(stdfs::current_path());

        stdfs::path file_name_path(TextureFname);
        file_name_path = file_name_path.filename();

        if (stdfs::exists(file_name_path))
        {
            return file_name_path.string();
        }


        stdfs::path file_path = starting_path;

        for (size_t i = 0; i < 4; ++i)
        {
            file_path = file_path.parent_path();
        }

        for (auto& dir_entry : stdfs::recursive_directory_iterator(file_path))
        {
            if (dir_entry == starting_path)
            {
                continue;
            }

            if (!stdfs::is_regular_file(dir_entry) || stdfs::is_directory(dir_entry))
            {
                continue;
            }

            const stdfs::path entry_path(dir_entry);
            const std::string curr_entry_str = entry_path.filename().string();

            if (case_insensitive_comparison(TextureFname, curr_entry_str))
            {
                return entry_path.string();
            }
        }

        return std::string();
    }

    void* loadImageDataFn(const char* fname, void* user_data)
    {
        namespace fs = std::experimental::filesystem;
        using namespace mango;
        auto& rsrc_context = ResourceContext::Get();
        const auto* device = RenderingContext::Get().Device();
        const uint32_t graphics_queue_idx = device->QueueFamilyIndices().Graphics;

        fs::path file_path(fname);
        std::string fileStr(fname);

        if (!fs::exists(file_path))
        {
            LOG(ERROR) << "Requested file path did not exist!";
            LOG(ERROR) << "File Path: " << fname;

            namespace fs = std::experimental::filesystem;
            static fs::path DEBUG_TEXTURE_PATH{ "../../../../assets/debug/FailedTexLoad.png" };
            if (!fs::exists(DEBUG_TEXTURE_PATH))
            {
                const std::string curr_dir = fs::current_path().string();
                const std::string DEBUG_TEX_STRING = ResourceLoader::FindFile(DEBUG_TEXTURE_PATH.filename().string(), curr_dir.c_str(), 3u);
                DEBUG_TEXTURE_PATH = fs::path(DEBUG_TEX_STRING);
            }

            fileStr = DEBUG_TEXTURE_PATH.string();

        }

        const static Format FORMAT_R16G16B16(16, mango::Format::UNORM, mango::Format::RGB, 16, 16, 16, 0);
        static const std::map<mango::Format, VkComponentMapping> formatComponentMappings{
            { FORMAT_A8, VkComponentMapping{ VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_R } },
            { FORMAT_L8, VkComponentMapping{ VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_R } },
            { FORMAT_R16, VkComponentMapping{ VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_R } },
            { FORMAT_L16, VkComponentMapping{ VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_R } },
            { FORMAT_L8A8, VkComponentMapping{ VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_ZERO, VK_COMPONENT_SWIZZLE_ZERO } },
            { FORMAT_R8G8B8A8, VkComponentMapping{ VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A } },
            { FORMAT_R8G8B8X8, VkComponentMapping{ VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_ZERO }},
            { FORMAT_B8G8R8, VkComponentMapping{ VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_ZERO }},
            { FORMAT_B8G8R8X8, VkComponentMapping{ VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_ZERO }},
            { FORMAT_B8G8R8A8, VkComponentMapping{ VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A }},
            { FORMAT_R16G16B16, VkComponentMapping{ VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_ZERO }}
        };


        loaded_texture_data_t* result = new loaded_texture_data_t(fileStr.c_str());

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

        if (result->bitmap.format == FORMAT_R16G16B16)
        {
            Bitmap dest_blit(result->bitmap.width, result->bitmap.height, FORMAT_R16G16B16A16);
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

        const VkImageCreateInfo img_create_info{
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

        const VkImageViewCreateInfo view_create_info{
            VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            nullptr,
            0,
            VK_NULL_HANDLE,
            VK_IMAGE_VIEW_TYPE_2D,
            image_format,
            view_mapping,
            VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
        };

        const std::string texture_name_str = fs::path(fname).filename().string();
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