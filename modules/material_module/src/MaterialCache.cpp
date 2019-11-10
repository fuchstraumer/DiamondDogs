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

struct MaterialCacheImpl
{
    MaterialCacheImpl() noexcept;
    ~MaterialCacheImpl();

    bool hasMaterial(const uint64_t hash);
    void createVkResources();

    std::atomic<bool> dirty{ true };
    // sets up vulkan resources for the material shader indices
    // atomic in the edge case two threads hit the code that flips initialized
    std::atomic<bool> initialized{ false };
    // used to give us available slots in containers
    // we can start at zero, as fetch_add returns the initial value (so we get 0u the first time instead of 1u)
    std::atomic<uint32_t> shaderIndicesIdx{ 0u };
    std::atomic<uint32_t> parametersIdx{ 0u };
    std::atomic<uint32_t> bindlessTexturesIdx{ 0u };
    /*
        We use this table to fetch indices for a material, and copy them
        into the indices array uploaded to the GPU. This way we can specify
        which materials are at which indices (in indicesArray) right before drawing - after
        whoever is using the material has done sorting of their drawcalls

        we have to do this as our materials are "per-draw", of sorts. we
        use the indirect drawing index to get our indices array (so, one instance
        per multidraw indirect structure) which then reads from the other VulkanResource
        arrays

        this way we can batch-dispatch up to MaxMaterialPageTextureCount draws
        and not have to rebind descriptor sets per drawcall (i.e, MaxMaterialPageTextureCount
        rebinds). plus, users can do sorting on their end and just deal with
        MaterialInstance handles
    */
    std::unique_ptr<DescriptorTemplate> descriptorTemplate;
    // sorted by material hash, and then each material has up to (TEXTURE_TYPES) number entries in the vector
    std::unordered_map<uint64_t, std::vector<image_loaded_callback_data_t>> callbackDataStorage;
    std::shared_mutex tableMutex;
    std::unordered_map<uint64_t, material_shader_indices_t> masterIndicesTable;
    std::unordered_map<uint64_t, bool> materialDirty;
    std::unordered_multimap<uint64_t, uint32_t> indicesUsingMaterial;
    std::unordered_map<texture_type, uint32_t> textureTypeBindingIndices;
    std::array<material_shader_indices_t, MaxMaterialPageTextureCount> indicesArray;
    // GPU-side data for the above
    VulkanResource* vkIndicesBufferSBO{ nullptr };
    // cpu-side parameter data. used to update parameterUBOs contents
    std::array<material_parameters_t, MaxMaterialPageTextureCount> parametersArray;
    using vulkan_resource_array_t = std::array<VulkanResource*, MaxMaterialPageTextureCount>;
    vulkan_resource_array_t parameterUBOs;
    vulkan_resource_array_t textures;
};

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
        std::atomic<size_t> NumUsages{ 0u };
    };
    static std::vector<resource_loader_data_t> loadedImages;
    // used for various static functionalities
    static std::mutex staticMaterialMutex;
    static std::shared_mutex materialSharedMutex;

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

    std::string ResolveTexturePath(const char* path, const char* mtlDir);
    uint64_t HashMaterialCreateInfo(const MaterialCreateInfo& createInfo);
    float extract_valid_material_param(const float input);
    VulkanResource* createUBO(const char* name, const material_parameters_t& params);
    void* loadImageDataFn(const char* fname, void* user_data);
    void destroyImageDataFn(void* object, void* user_data);

}

// used when a material explicitly fails to load. classic pink/black squares.
static VulkanResource* failedLoadDebugTexture{ nullptr };
static std::unordered_map<uint64_t, std::map<texture_type, resource_loader_data_t>> loaderDataMap;

MaterialCacheImpl::MaterialCacheImpl() noexcept : descriptorTemplate(std::make_unique<DescriptorTemplate>("MaterialCachePageDescriptorTemplate"))
{
    parameterUBOs.fill(nullptr);
    textures.fill(nullptr);
}

MaterialCacheImpl::~MaterialCacheImpl() {}

void MaterialCacheImpl::createVkResources()
{
    static size_t numPages{ 0u };

    if (initialized)
    {
        return;
    }
    else
    {
        initialized = true;
    }

    masterIndicesTable.reserve(MaxMaterialPageTextureCount);
    materialDirty.reserve(MaxMaterialPageTextureCount);
    indicesUsingMaterial.reserve(MaxMaterialPageTextureCount * 8u);
    textureTypeBindingIndices.reserve(MaxMaterialPageTextureCount * 16u);
    callbackDataStorage.reserve(MaxMaterialPageTextureCount * 4u);

    constexpr static VkBufferCreateInfo indicesBufferInfo
    {
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        nullptr,
        0,
        sizeof(material_shader_indices_t) * MaxMaterialPageTextureCount,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_SHARING_MODE_EXCLUSIVE,
        0u,
        nullptr
    };

    auto& rsrc_context = ResourceContext::Get();
    const std::string resourceName = "MaterialCacheIndicesBufferSBO_" + std::to_string(numPages);
    vkIndicesBufferSBO = rsrc_context.CreateBuffer(&indicesBufferInfo, nullptr, 0u, nullptr, resource_usage::CPU_ONLY, ResourceCreateMemoryStrategyMinFragmentation | ResourceCreateUserDataAsString, (void*)resourceName.c_str());

}

MaterialCache::MaterialCache()
{
    loaderDataMap.reserve(MaxMaterialPageTextureCount * 8u);
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

    for (auto materialIter = loaderDataMap.begin(); materialIter != loaderDataMap.end(); ++materialIter)
    {
        for (auto imagesIter = materialIter->second.begin(); imagesIter != materialIter->second.end(); ++imagesIter)
        {
            if (!rsrc_context.ResourceInTransferQueue(imagesIter->second.CreatedResource))
            {
                const char* fileType = imagesIter->second.FileType.c_str();
                const char* fileName = imagesIter->second.FilePath.c_str();
                for (size_t i = 0; i < imagesIter->second.NumUsages; ++i)
                {
                    // need to get ref count to zero
                    rsrc_loader.Unload(fileType, fileName);
                }
            }
        }
    }

}

MaterialInstance MaterialCache::CreateMaterial(const MaterialCreateInfo& createInfo)
{
    auto& resource_loader = ResourceLoader::GetResourceLoader();
    resource_loader.Subscribe("MANGO", loadImageDataFn, destroyImageDataFn);

    const uint64_t materialHash = HashMaterialCreateInfo(createInfo);

    rw_lock_guard dataGuard(lock_mode::Read, materialSharedMutex);

    impl->createVkResources();

    if (impl->hasMaterial(materialHash))
    {
        /*
            The material may still be loading, but that doesn't really matter.
            We'll update the data before we draw.
        */
        MaterialInstance result;
        result.MaterialHash = materialHash;
        result.PageIdx = 0u;
        auto& entry = loaderDataMap.at(materialHash).at(texture_type::Ambient);
        entry.NumUsages++;
        return result;
    }
    else
    {
        const uint32_t materialCount = static_cast<uint32_t>(impl->masterIndicesTable.size());
        if (materialCount > MaxMaterialPageTextureCount)
        {
            // oh no....
            // (I'm not implementing this yet as my head hurts okay)
            // (I have plans but seriously this is gonna suck I think)
            throw std::runtime_error("Ran out of space in material page!");
        }

        // new material that hasn't been seen yet: create a new indices structure
        dataGuard.upgrade_to_write_mode();
        auto indicesIter = impl->masterIndicesTable.emplace(materialHash, material_shader_indices_t{});
        assert(indicesIter.second);

        auto load_texture = [this, materialHash, &createInfo](const texture_type type, const char* fileName)
        {
            auto& resource_loader = ResourceLoader::GetResourceLoader();
            auto& callbackStorage = impl->callbackDataStorage;

            if (callbackStorage.count(materialHash) == 0u)
            {
                // construct vector of right size for us to use
                callbackStorage[materialHash] = std::vector<image_loaded_callback_data_t>(static_cast<size_t>(texture_type::Count), image_loaded_callback_data_t{});
            }

            // storage vector for this material
            auto& materialStorageVec = callbackStorage.at(materialHash);

            materialStorageVec[static_cast<size_t>(type)] = image_loaded_callback_data_t{ materialHash, type };
            auto* callbackData = &materialStorageVec[static_cast<size_t>(type)];

            auto& entry = loaderDataMap[materialHash][type];
            entry.NumUsages++;
            entry.FilePath = fileName;
            entry.FileType = "MANGO";

            resource_loader.Load("MANGO", fileName, createInfo.MaterialDirectory, this, textureLoadedCallback, callbackData);
        };


        bool hasBumpMap = false;
        bool hasDisplacementMap = false;
        
        for (size_t i = 0; i < createInfo.NumTextures; ++i)
        {
            hasDisplacementMap = createInfo.Types[i] == texture_type::Displacement ? true : false;
            hasBumpMap = createInfo.Types[i] == texture_type::Bump ? true : false;
            if (hasDisplacementMap && hasBumpMap)
            {
                LOG(ERROR) << "Material being loaded has both a bump map and a displacement map, which is invalid!";
            }
            load_texture(createInfo.Types[i], createInfo.TexturePaths[i]);
        }

        // index for UBO is gonna be unique for each unique material, since every material has a UBO
        const uint32_t uboIdx = impl->parametersIdx.fetch_add(1u);

        dataGuard.release_lock(); // don't need lock anymore: no iterators to invalidate like we can w unordered_map

        impl->parametersArray[uboIdx] = createInfo.Parameters;
        // this is quick enough that it's not worth running through the loader
        impl->parameterUBOs[uboIdx] = createUBO(createInfo.MaterialName, impl->parametersArray[uboIdx]);

        // makes sure we update descriptors before use
        impl->dirty = true;

    }

    return MaterialInstance{ materialHash, 0u };
}

void MaterialCache::UseMaterialAtIdx(const MaterialInstance& instance, const uint32_t idx)
{
    rw_lock_guard tableGuard(lock_mode::Write, impl->tableMutex);
    impl->indicesUsingMaterial.emplace(instance.MaterialHash, idx);
}

void MaterialCache::textureLoadedCallbackImpl(void* loaded_image, void* user_data)
{
    loaded_texture_data_t* textureData = reinterpret_cast<loaded_texture_data_t*>(loaded_image);
    image_loaded_callback_data_t* callbackData = reinterpret_cast<image_loaded_callback_data_t*>(user_data);

    const texture_type loadedTexType = callbackData->type;

    rw_lock_guard tableGuard(lock_mode::Write, impl->tableMutex);
    auto indexIter = impl->masterIndicesTable.find(callbackData->mtlHash);
    assert(indexIter != std::end(impl->masterIndicesTable));
    auto& indices = indexIter->second;
    auto& resourceLoaderData = loaderDataMap.at(callbackData->mtlHash);

    auto set_texture_at_idx = [&textureData, this](int32_t& idxToSet)
    {
        const uint32_t idxToUse = impl->bindlessTexturesIdx.fetch_add(1u);
        impl->textures[idxToUse] = textureData->resource;
        idxToSet = static_cast<int32_t>(idxToUse);
    };

    impl->materialDirty[callbackData->mtlHash] = true;

    switch (loadedTexType)
    {
    case texture_type::Ambient:
        set_texture_at_idx(indices.AoMapIdx);
        resourceLoaderData[texture_type::Ambient].CreatedResource = textureData->resource;
        break;
    case texture_type::Diffuse:
        set_texture_at_idx(indices.AlbedoIdx);
        resourceLoaderData[texture_type::Diffuse].CreatedResource = textureData->resource;
        break;
    case texture_type::Specular:
        set_texture_at_idx(indices.SpecularIdx);
        resourceLoaderData[texture_type::Specular].CreatedResource = textureData->resource;
        break;
    case texture_type::SpecularHighlight:
        LOG(WARNING) << "Tried to use unsupported sheen texture type.";
        break;
    case texture_type::Bump:
        set_texture_at_idx(indices.BumpMapIdx);
        resourceLoaderData[texture_type::Bump].CreatedResource = textureData->resource;
        break;
    case texture_type::Displacement:
        set_texture_at_idx(indices.DisplacementMapIdx);
        resourceLoaderData[texture_type::Displacement].CreatedResource = textureData->resource;
        break;
    case texture_type::Alpha:
        set_texture_at_idx(indices.AlphaIdx);
        resourceLoaderData[texture_type::Alpha].CreatedResource = textureData->resource;
        break;
    case texture_type::Roughness:
        set_texture_at_idx(indices.RoughnessMapIdx);
        resourceLoaderData[texture_type::Roughness].CreatedResource = textureData->resource;
        break;
    case texture_type::Metallic:
        set_texture_at_idx(indices.MetallicMapIdx);
        resourceLoaderData[texture_type::Metallic].CreatedResource = textureData->resource;
        break;
    case texture_type::Sheen:
        LOG(WARNING) << "Tried to use unsupported sheen texture type.";
        break;
    case texture_type::Emissive:
        set_texture_at_idx(indices.EmissiveMapIdx);
        resourceLoaderData[texture_type::Emissive].CreatedResource = textureData->resource;
        break;
    case texture_type::Normal:
        set_texture_at_idx(indices.NormalMapIdx);
        resourceLoaderData[texture_type::Normal].CreatedResource = textureData->resource;
        break;
    default:
        throw std::domain_error("Texture type loaded was of invalid type!");
    };

    auto iter = impl->callbackDataStorage.find(callbackData->mtlHash);
    if (iter != std::end(impl->callbackDataStorage))
    {
        auto& storageVec = iter->second;
        storageVec[static_cast<size_t>(callbackData->type)] = image_loaded_callback_data_t{};
    }
    else
    {
        throw std::runtime_error("Couldn't find callback data, page must be incorrect!");
    }
}

void MaterialCache::updatePage()
{
    if (impl->dirty)
    {
        rw_lock_guard pageTableGuard(lock_mode::Write, impl->tableMutex);

        for (auto& mtl : impl->materialDirty)
        {
            auto indices_range = impl->indicesUsingMaterial.equal_range(mtl.first);
            for (auto iter = indices_range.first; iter != indices_range.second; ++iter)
            {
                impl->indicesArray[iter->second] = impl->masterIndicesTable.at(iter->first);
            }
        }

        const gpu_resource_data_t indicesArrayData
        {
            &impl->indicesArray, MaxMaterialPageTextureCount * sizeof(material_shader_indices_t),
            0u, VK_QUEUE_FAMILY_IGNORED
        };

        auto& rsrc_context = ResourceContext::Get();
        rsrc_context.SetBufferData(impl->vkIndicesBufferSBO, 1u, &indicesArrayData);

        // now update image handles


        impl->materialDirty.clear();
    }

    impl->dirty = false;
}

constexpr bool image_loaded_callback_data_t::operator==(const image_loaded_callback_data_t& other) const noexcept
{
    return (mtlHash == other.mtlHash) && (type == other.type);
}

constexpr bool image_loaded_callback_data_t::operator<(const image_loaded_callback_data_t& other) const noexcept
{
    if (mtlHash == other.mtlHash)
    {
        return type < other.type;
    }
    else
    {
        return mtlHash < other.mtlHash;
    }
}

void textureLoadedCallback(void* instance, void* loaded_data, void* user_data)
{
    MaterialCache* cacheInstance = reinterpret_cast<MaterialCache*>(instance);
    cacheInstance->textureLoadedCallbackImpl(loaded_data, user_data);
}

constexpr bool MaterialInstance::operator==(const MaterialInstance& other) const noexcept
{
    return (MaterialHash == other.MaterialHash) && (PageIdx == other.PageIdx);
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

    std::string ResolveTexturePath(const char* input_path, const char* mtlDir)
    {
        namespace stdfs = std::filesystem;
        stdfs::path texPath(input_path);
        if (stdfs::exists(texPath))
        {
            // make canonical to cut out extra fluff, and get full user-local string
            return stdfs::canonical(texPath).string();
        }
        else
        {
            // now we gotta find the file
            return ResourceLoader::FindFile(texPath.filename().string(), mtlDir, 3u);
        }
    }

    uint64_t HashMaterialCreateInfo(const MaterialCreateInfo& createInfo)
    {
        // Params hash hashes contents as well: difference in values will be invalid
        const uint64_t paramsHash = MurmurHash2(&createInfo.Parameters, sizeof(material_parameters_t), 0xFD);
        std::string concatenatedNames{ createInfo.MaterialName };
        concatenatedNames.reserve(1024);
        for (size_t i = 0; i < createInfo.NumTextures; ++i)
        {
            concatenatedNames += std::string(createInfo.TexturePaths[i]);
        }
        concatenatedNames.shrink_to_fit();
        const uint64_t namesHash = MurmurHash2((void*)concatenatedNames.c_str(), sizeof(char) * concatenatedNames.length(), 0xFC);
        return namesHash ^ paramsHash;
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