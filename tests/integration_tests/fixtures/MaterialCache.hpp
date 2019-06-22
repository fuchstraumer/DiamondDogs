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
constexpr static size_t MaxMaterialPoolTextureCount = 256u;
// Max of 8192 textures loaded at once
constexpr static size_t MaxMaterialPages = 32u;

namespace tinyobj_opt
{
    struct material_t;
}

struct MaterialInstance
{
    uint64_t HashCode{ std::numeric_limits<uint64_t>::max() };
};

/*
    Since the resource loader already caches a lot of the loaded
    data for us, this system doesn't actually cache much of the 
    data just the handles and some material-specific data we generate
*/
class MaterialPool
{
    MaterialPool(const MaterialPool&) = delete;
    MaterialPool& operator=(const MaterialPool&) = delete;
    MaterialPool();
public:

    MaterialPool& Get() noexcept;

    MaterialInstance* LoadTinyobjMaterial(const tinyobj_opt::material_t& mtl, const char* material_dir);
    

private:

    struct atomic_container_indices_t
    {
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

    /*
        If we exceed MaxMaterialPoolTextureCount, we just create a new one of these
        and start using that. 
    */
    struct material_cache_page_t
    {
        material_cache_page_t() noexcept;
        
        std::atomic<uint32_t> currTextureCount{ 0u };
        // used to give us available slots in containers
        atomic_container_indices_t containerIndices;
        // converts material hash to index into indices array for that material
        std::unordered_map<uint64_t, uint32_t> hashToIndicesMap;
        std::array<material_shader_indices_t, MaxMaterialPoolTextureCount> indicesArray;
        std::array<std::string, MaxMaterialPoolTextureCount> namesArray;
        // cpu-side parameter data. used to update parameterUBOs contents
        std::array<material_parameters_t, MaxMaterialPoolTextureCount> parametersArray;
        using vulkan_resource_array_t = std::array<VulkanResource*, MaxMaterialPoolTextureCount>;
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
