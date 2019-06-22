#pragma once
#ifndef DIAMOND_DOGS_MATERIAL_POOL_HPP
#define DIAMOND_DOGS_MATERIAL_POOL_HPP
#include "ForwardDecl.hpp"
#include "ResourceTypes.hpp"
#include "MaterialStructures.hpp"
#include <cstdint>
#include <array>
#include <unordered_map>
#include <atomic>

// Also sets max size of a drawcall
constexpr static size_t MaxMaterialCacheTextureCount = 256u;
// Max of 8192 textures loaded at once
constexpr static size_t MaxMaterialPages = 32u;

struct MaterialInstance
{
    uint64_t HashCode{ std::numeric_limits<uint64_t>::max() };
};

/*
    Since the resource loader already caches a lot of the loaded
    data for us, this system doesn't actually cache much of the 
    data just the handles and some material-specific data we generate
*/
class MaterialCache
{
    MaterialCache(const MaterialCache&) = delete;
    MaterialCache& operator=(const MaterialCache&) = delete;
    MaterialCache();
public:

    MaterialCache& Get() noexcept;
    // Releases CPU-side image data. Call at most once per frame (more won't break, just won't be worth it)
    static void ReleaseCpuData();

    MaterialInstance* LoadTinyobjMaterial(const void* mtl, const char* material_dir);
    

private:

    void textureLoadedCallback(void* loaded_image, void* user_data);

    struct atomic_container_indices_t
    {
        // we can start at zero, as fetch_add returns the initial value (so we get 0u the first time instead of 1u)
        std::atomic<uint32_t> shaderIndicesIdx{ 0u };
        std::atomic<uint32_t> parametersCpuIdx{ 0u };
        std::atomic<uint32_t> parametersGpuIdx{ 0u };
        std::atomic<uint32_t> albedoMapIdx{ 0u };
        std::atomic<uint32_t> alphaMapsIdx{ 0u };
        std::atomic<uint32_t> specularMapsIdx{ 0u };
        std::atomic<uint32_t> bumpMapsIdx{ 0u };
        std::atomic<uint32_t> displacementMapsIdx{ 0u };
        std::atomic<uint32_t> normalMapsIdx{ 0u };
        std::atomic<uint32_t> aoMapsIdx{ 0u };
        std::atomic<uint32_t> metallicMapsIdx{ 0u };
        std::atomic<uint32_t> roughnessMapsIdx{ 0u };
    };

    struct image_loaded_callback_data_t
    {
        uint64_t mtlHash;
        texture_type Type;
    };

    /*
        If we exceed MaxMaterialCacheTextureCount, we just create a new one of these
        and start using that. 
    */
    struct material_cache_page_t
    {
        material_cache_page_t() noexcept;
        
        std::atomic<uint32_t> currMaterialCount{ 0u };
        // used to give us available slots in containers
        atomic_container_indices_t containerIndices;
        /*
            hashToIndicesMap specifies indices (plural!) in the arrays below 
            that use that material. we use a multimap as this way if we have
            the material already, we can just copy the indices array.

            we have to do this as our materials are "per-draw", of sorts. we
            use the indirect drawing index to get our indices array (so, one instance
            per multidraw indirect structure) which then reads from the other VulkanResource
            arrays

            this way we can batch-dispatch up to MaxMaterialCacheTextureCount draws
            and not have to rebind descriptor sets per drawcall (i.e, MaxMaterialCacheTextureCount
            rebinds).
        */
        std::unordered_multimap<uint64_t, uint32_t> hashToIndicesMap;
        std::array<material_shader_indices_t, MaxMaterialCacheTextureCount> indicesArray;
        // GPU-side data for the above
        std::array<VulkanResource*, MaxMaterialCacheTextureCount> vkIndicesArray;
        std::array<std::string, MaxMaterialCacheTextureCount> namesArray;
        // cpu-side parameter data. used to update parameterUBOs contents
        std::array<material_parameters_t, MaxMaterialCacheTextureCount> parametersArray;
        using vulkan_resource_array_t = std::array<VulkanResource*, MaxMaterialCacheTextureCount>;
        vulkan_resource_array_t parameterUBOs;
        vulkan_resource_array_t albedoMaps;
        vulkan_resource_array_t alphaMaps;
        vulkan_resource_array_t specularMaps;
        vulkan_resource_array_t bumpMaps;
        vulkan_resource_array_t displacementMaps;
        vulkan_resource_array_t normalMaps;
        vulkan_resource_array_t aoMaps;
        vulkan_resource_array_t metallicMaps;
        vulkan_resource_array_t roughnessMaps;
        vulkan_resource_array_t emissiveMaps;
    };

    std::atomic<uint32_t> currentPage{ 0u };
    std::array<material_cache_page_t, MaxMaterialPages> materialPages;

};

#endif //!DIAMOND_DOGS_MATERIAL_POOL_HPP
