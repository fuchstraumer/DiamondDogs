#include "MaterialCache.hpp"
#include "RenderingContext.hpp"
#include "ResourceContext.hpp"
#include "ResourceLoader.hpp"
#include "LogicalDevice.hpp"
#include "DescriptorTemplate.hpp"
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

    float extract_valid_material_param(const float input);
    glm::vec3 extract_valid_material_param(const glm::vec3& in) noexcept;
    material_parameters_t from_tinyobj_material(const tinyobj_opt::material_t& mtl) noexcept;
    VulkanResource* createUBO(const char* name, const material_parameters_t& params);
    std::string FindDebugTexture();
    void* loadImageDataFn(const char* fname, void* user_data);
    void destroyImageDataFn(void* object, void* user_data);

}

// used when a material explicitly fails to load. classic pink/black squares.
static VulkanResource* failedLoadDebugTexture{ nullptr };
static std::unordered_map<uint64_t, std::map<texture_type, resource_loader_data_t>> loaderDataMap;

MaterialCache::material_cache_page_t::material_cache_page_t() noexcept : descriptorTemplate(std::make_unique<DescriptorTemplate>("MaterialCachePageDescriptorTemplate"))
{
    parameterUBOs.fill(nullptr);
    textures.fill(nullptr);
}

MaterialCache::material_cache_page_t::~material_cache_page_t() {}

void MaterialCache::material_cache_page_t::createVkResources()
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
    const std::string resourceName = "MaterialCachePageIndicesBufferSBO_" + std::to_string(numPages);
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

MaterialInstance MaterialCache::LoadTinyobjMaterial(const void* mtlPtr, const char* material_dir)
{
    auto& resource_loader = ResourceLoader::GetResourceLoader();
    resource_loader.Subscribe("MANGO", loadImageDataFn, destroyImageDataFn);

    assert(mtlPtr != nullptr);
    const auto& mtl = *reinterpret_cast<const tinyobj_opt::material_t*>(mtlPtr);

    material_parameters_t params = from_tinyobj_material(mtl);

    const uint64_t materialHash = hashTinyobjMaterial(mtlPtr, params);

    auto& currMtlPage = materialPage;
    currMtlPage.createVkResources();

    rw_lock_guard dataGuard(lock_mode::Read, materialSharedMutex);

    if (currMtlPage.masterIndicesTable.count(materialHash) != 0u)
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
        const uint32_t materialCount = static_cast<uint32_t>(currMtlPage.masterIndicesTable.size());
        if (materialCount > MaxMaterialPageTextureCount)
        {
            // oh no....
            // (I'm not implementing this yet as my head hurts okay)
            // (I have plans but seriously this is gonna suck I think)
            throw std::runtime_error("Ran out of space in material page!");
        }

        // new material that hasn't been seen yet: create a new indices structure
        dataGuard.upgrade_to_write_mode();
        auto indicesIter = currMtlPage.masterIndicesTable.emplace(materialHash, material_shader_indices_t{});
        assert(indicesIter.second);

        // keep the mutex in write mode: we might emplace in some other
        // containers yet that have to deal with iterator invalidation
        const bool hasAmbientTexture = !mtl.ambient_texname.empty();
        const bool hasDiffuseTexture = !mtl.diffuse_texname.empty();
        const bool hasSpecularTexture = !mtl.specular_texname.empty();
        const bool hasSpecularHighlightTexture = !mtl.specular_highlight_texname.empty();
        const bool hasBumpMap = !mtl.bump_texname.empty();
        const bool hasDisplacementMap = !mtl.displacement_texname.empty();
        const bool hasAlphaTexture = !mtl.alpha_texname.empty();
        const bool hasRoughnessTexture = !mtl.roughness_texname.empty();
        const bool hasMetallicTexture = !mtl.metallic_texname.empty();
        const bool hasSheenTexture = !mtl.sheen_texname.empty();
        const bool hasEmissiveTexture = !mtl.emissive_texname.empty();
        const bool hasNormalTexture = !mtl.normal_texname.empty();

        auto load_texture = [this, materialHash, &currMtlPage, &mtl, &material_dir](const texture_type type, const char* fileName)
        {
            auto& resource_loader = ResourceLoader::GetResourceLoader();
            auto& callbackStorage = currMtlPage.callbackDataStorage;

            if (callbackStorage.count(materialHash) == 0u)
            {
                // construct vector of right size for us to use
                callbackStorage[materialHash] = std::vector<image_loaded_callback_data_t>(static_cast<size_t>(texture_type::Count), image_loaded_callback_data_t{});
            }

            // storage vector for this material
            auto& materialStorageVec = currMtlPage.callbackDataStorage.at(materialHash);

            materialStorageVec[static_cast<size_t>(type)] = image_loaded_callback_data_t{ materialHash, type };
            auto* callbackData = &materialStorageVec[static_cast<size_t>(type)];

            auto& entry = loaderDataMap[materialHash][type];
            entry.NumUsages++;
            entry.FilePath = fileName;
            entry.FileType = "MANGO";

            resource_loader.Load("MANGO", fileName, material_dir, this, textureLoadedCallback, callbackData);
        };

        // now the tricky part: loading textures
        if (hasAmbientTexture)
        {
            load_texture(texture_type::Ambient, mtl.ambient_texname.c_str());
        }

        if (hasDiffuseTexture)
        {
            load_texture(texture_type::Diffuse, mtl.diffuse_texname.c_str());
        }

        if (hasSpecularTexture)
        {
            load_texture(texture_type::Specular, mtl.specular_texname.c_str());
        }

        if (hasSpecularHighlightTexture)
        {
            load_texture(texture_type::SpecularHighlight, mtl.specular_highlight_texname.c_str());
        }

        if (hasBumpMap)
        {
            LOG_IF(hasDisplacementMap, WARNING) << "Material has both a bump map and displacement map, which will be treated as redundant by the shader.";
            load_texture(texture_type::Bump, mtl.bump_texname.c_str());
        }

        if (hasDisplacementMap)
        {
            LOG_IF(hasBumpMap, WARNING) << "Material has both a bump map and displacement map, which will be treated as redundant by the shader.";
            load_texture(texture_type::Displacement, mtl.displacement_texname.c_str());
        }

        if (hasAlphaTexture)
        {
            load_texture(texture_type::Alpha, mtl.alpha_texname.c_str());
        }

        if (hasRoughnessTexture)
        {
            load_texture(texture_type::Roughness, mtl.roughness_texname.c_str());
        }

        if (hasMetallicTexture)
        {
            load_texture(texture_type::Metallic, mtl.metallic_texname.c_str());
        }

        if (hasSheenTexture)
        {
            LOG(ERROR) << "Sheen texture is unsupported!";
        }

        if (hasEmissiveTexture)
        {
            load_texture(texture_type::Emissive, mtl.emissive_texname.c_str());
        }

        if (hasNormalTexture)
        {
            load_texture(texture_type::Normal, mtl.normal_texname.c_str());
        }

        dataGuard.release_lock(); // don't need lock anymore: no iterators to invalidate like we can w unordered_map

        // index for UBO is gonna be unique for each unique material, since every material has a UBO
        const uint32_t uboIdx = currMtlPage.parametersIdx.fetch_add(1u);
        currMtlPage.parametersArray[uboIdx] = std::move(params);
        // this is quick enough that it's not worth running through the loader
        currMtlPage.parameterUBOs[uboIdx] = createUBO(mtl.name.c_str(), currMtlPage.parametersArray[uboIdx]);

        // makes sure we update descriptors before use
        currMtlPage.dirty = true;

    }

    return MaterialInstance{ materialHash, 0u };
}

void MaterialCache::UseMaterialAtIdx(const MaterialInstance& instance, const uint32_t idx)
{
    rw_lock_guard tableGuard(lock_mode::Write, materialPage.tableMutex);
    materialPage.indicesUsingMaterial.emplace(instance.MaterialHash, idx);
}

void MaterialCache::textureLoadedCallbackImpl(void* loaded_image, void* user_data)
{
    loaded_texture_data_t* textureData = reinterpret_cast<loaded_texture_data_t*>(loaded_image);
    image_loaded_callback_data_t* callbackData = reinterpret_cast<image_loaded_callback_data_t*>(user_data);

    const texture_type loadedTexType = callbackData->type;
    material_cache_page_t& currPage = materialPage;

    rw_lock_guard tableGuard(lock_mode::Write, currPage.tableMutex);
    auto indexIter = currPage.masterIndicesTable.find(callbackData->mtlHash);
    assert(indexIter != std::end(currPage.masterIndicesTable));
    auto& indices = indexIter->second;
    auto& resourceLoaderData = loaderDataMap.at(callbackData->mtlHash);

    auto set_texture_at_idx = [&textureData, &currPage](int32_t& idxToSet)
    {
        const uint32_t idxToUse = currPage.bindlessTexturesIdx.fetch_add(1u);
        currPage.textures[idxToUse] = textureData->resource;
        idxToSet = static_cast<int32_t>(idxToUse);
    };

    currPage.materialDirty[callbackData->mtlHash] = true;

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

    auto iter = currPage.callbackDataStorage.find(callbackData->mtlHash);
    if (iter != std::end(currPage.callbackDataStorage))
    {
        auto& storageVec = iter->second;
        storageVec[static_cast<size_t>(callbackData->type)] = image_loaded_callback_data_t{};
    }
    else
    {
        throw std::runtime_error("Couldn't find callback data, page must be incorrect!");
    }
}

uint64_t MaterialCache::hashTinyobjMaterial(const void* mtlPtr, const material_parameters_t& params)
{
    // Params hash hashes contents as well: difference in values will be invalid
    const uint64_t paramsHash = MurmurHash2(&params, sizeof(params), 0xFD);
    const tinyobj_opt::material_t& mtl = *reinterpret_cast<const tinyobj_opt::material_t*>(mtlPtr);
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
    return namesHash ^ paramsHash;
}

void MaterialCache::updatePage()
{
    auto& pageToUpdate = materialPage;
    if (pageToUpdate.dirty)
    {
        rw_lock_guard pageTableGuard(lock_mode::Write, pageToUpdate.tableMutex);

        for (auto& mtl : pageToUpdate.materialDirty)
        {
            auto indices_range = pageToUpdate.indicesUsingMaterial.equal_range(mtl.first);
            for (auto iter = indices_range.first; iter != indices_range.second; ++iter)
            {
                pageToUpdate.indicesArray[iter->second] = pageToUpdate.masterIndicesTable.at(iter->first);
            }
        }

        const gpu_resource_data_t indicesArrayData
        {
            &pageToUpdate.indicesArray, MaxMaterialPageTextureCount * sizeof(material_shader_indices_t),
            0u, VK_QUEUE_FAMILY_IGNORED
        };

        auto& rsrc_context = ResourceContext::Get();
        rsrc_context.SetBufferData(pageToUpdate.vkIndicesBufferSBO, 1u, &indicesArrayData);

        // now update image handles


        pageToUpdate.materialDirty.clear();
    }

    pageToUpdate.dirty = false;
}

constexpr bool MaterialCache::image_loaded_callback_data_t::operator==(const image_loaded_callback_data_t& other) const noexcept
{
    return (mtlHash == other.mtlHash) && (type == other.type);
}

constexpr bool MaterialCache::image_loaded_callback_data_t::operator<(const image_loaded_callback_data_t& other) const noexcept
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

void textureLoadedCallback(void* instance, void* loaded_data, void* user_data)
{
    MaterialCache* cacheInstance = reinterpret_cast<MaterialCache*>(instance);
    cacheInstance->textureLoadedCallbackImpl(loaded_data, user_data);
}

constexpr bool MaterialInstance::operator==(const MaterialInstance& other) const noexcept
{
    return (MaterialHash == other.MaterialHash) && (PageIdx == other.PageIdx);
}
