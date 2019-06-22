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
#include <shared_mutex>
#include <set>

// Also sets max size of a drawcall
constexpr static size_t MaxMaterialPageTextureCount = 512u;
// Max of 8192 textures loaded at once
constexpr static size_t MaxMaterialPages = 16u;

struct MaterialInstance
{
    // what material this instance is using
    uint64_t MaterialHash{ std::numeric_limits<uint64_t>::max() };
    // separates material pages / drawcall batches
    uint64_t PageIdx{ std::numeric_limits<uint64_t>::max() };
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

    MaterialInstance LoadTinyobjMaterial(const void* mtl, const char* material_dir);

    // Sets material to use at (idx) on material page from instance
    void UseMaterialAtIdx(const MaterialInstance& instance, const uint32_t idx);

private:

    friend void textureLoadedCallback(void* instance, void* loaded_data, void* user_data);
    void textureLoadedCallbackImpl(void* loaded_image, void* user_data);
    uint64_t hashTinyobjMaterial(const void* mtlPtr, const material_parameters_t& params);

    struct atomic_container_indices_t
    {
        // we can start at zero, as fetch_add returns the initial value (so we get 0u the first time instead of 1u)
        std::atomic<uint32_t> shaderIndicesIdx{ 0u };
        std::atomic<uint32_t> parametersIdx{ 0u };
        std::atomic<uint32_t> albedoMapsIdx{ 0u };
        std::atomic<uint32_t> alphaMapsIdx{ 0u };
        std::atomic<uint32_t> specularMapsIdx{ 0u };
        std::atomic<uint32_t> bumpMapsIdx{ 0u };
        std::atomic<uint32_t> displacementMapsIdx{ 0u };
        std::atomic<uint32_t> normalMapsIdx{ 0u };
        std::atomic<uint32_t> aoMapsIdx{ 0u };
        std::atomic<uint32_t> metallicMapsIdx{ 0u };
        std::atomic<uint32_t> roughnessMapsIdx{ 0u };
        std::atomic<uint32_t> emissiveMapsIdx{ 0u };
    };

    struct image_loaded_callback_data_t
    {
        uint64_t mtlHash;
        uint64_t pageIdx;
        texture_type type;
        constexpr bool operator==(const image_loaded_callback_data_t& other) const noexcept;
        constexpr bool operator<(const image_loaded_callback_data_t& other) const noexcept;
    };

    /*
        If we exceed MaxMaterialPageTextureCount, we just create a new one of these
        and start using that. 
    */
    struct material_cache_page_t
    {
        material_cache_page_t() noexcept;

        void createVkResources();
        
        bool dirty{ true };
        // sets up vulkan resources for the material shader indices
        // atomic in the edge case two threads hit the code that flips initialized
        std::atomic<bool> initialized{ false };
        // used to give us available slots in containers
        atomic_container_indices_t containerIndices;
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
        std::set<image_loaded_callback_data_t> callbackDataStorage;
        std::unordered_map<uint64_t, material_shader_indices_t> masterIndicesTable;
        std::shared_mutex indicesTableMutex;
        std::array<material_shader_indices_t, MaxMaterialPageTextureCount> indicesArray;
        // GPU-side data for the above
        VulkanResource* vkIndicesBufferSBO{ nullptr };
        // cpu-side parameter data. used to update parameterUBOs contents
        std::array<material_parameters_t, MaxMaterialPageTextureCount> parametersArray;
        using vulkan_resource_array_t = std::array<VulkanResource*, MaxMaterialPageTextureCount>;
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

    std::atomic<uint64_t> currentPage{ 0u };
    std::array<material_cache_page_t, MaxMaterialPages> materialPages;

};

#endif //!DIAMOND_DOGS_MATERIAL_POOL_HPP
