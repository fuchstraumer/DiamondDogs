#pragma once
#ifndef DIAMOND_DOGS_MATERIAL_POOL_HPP
#define DIAMOND_DOGS_MATERIAL_POOL_HPP
#include "ForwardDecl.hpp"
#include "ResourceTypes.hpp"
#include "MaterialStructures.hpp"
#include <cstdint>
#include <memory>

// Also sets max size of a drawcall: most platforms have limits way higher than this anyways
constexpr static size_t MaxMaterialPageTextureCount = 32768u;
// Max of 8192 textures loaded at once
constexpr static size_t MaxMaterialPages = 16u;

class DescriptorTemplate;
struct MaterialCacheImpl;

struct MaterialInstance
{
    // what material this instance is using
    uint64_t MaterialHash{ std::numeric_limits<uint64_t>::max() };
    // separates material pages / drawcall batches
    uint64_t PageIdx{ std::numeric_limits<uint64_t>::max() };
    constexpr bool operator==(const MaterialInstance& other) const noexcept;
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
    ~MaterialCache();
public:

    static MaterialCache& Get() noexcept;
    // Releases CPU-side image data. Call at most once per frame (more won't break, just won't be worth it)
    static void ReleaseCpuData();

    MaterialInstance CreateMaterial(const MaterialCreateInfo& createInfo);

    // Sets material to use at (idx) on material page from instance
    void UseMaterialAtIdx(const MaterialInstance& instance, const uint32_t idx);
    void BindPage();

private:

    std::unique_ptr<MaterialCacheImpl> impl{ nullptr };

    friend void textureLoadedCallback(void* instance, void* loaded_data, void* user_data);
    void textureLoadedCallbackImpl(void* loaded_image, void* user_data);
    void updatePage();


};

#endif //!DIAMOND_DOGS_MATERIAL_POOL_HPP
