#ifndef TEST_FIXTURES_MATERIAL_HPP
#define TEST_FIXTURES_MATERIAL_HPP
#include "ForwardDecl.hpp"
#include "ResourceTypes.hpp"
#pragma warning(push, 1)
#include "tinyobj_loader_opt.h"
#pragma warning(pop)
#include "MaterialStructures.hpp"
#include <vulkan/vulkan.h>
#include <atomic>

class DescriptorBinder;
class Descriptor;

class Material
{
    Material(const Material&) = delete;
    Material& operator=(const Material&) = delete;

public:

    Material() = default;
    Material(const tinyobj_opt::material_t& mtl, const char* material_dir);
    Material(Material&& other) noexcept;
    Material& operator=(Material&& other) noexcept;
    ~Material();

    void PopulateDescriptor(Descriptor& descr) const;
    void Bind(VkCommandBuffer cmd, const VkPipelineLayout layout, DescriptorBinder& binder);
    bool Opaque() const noexcept;

    static Material CreateDebugMaterial();


private:

    void setParameters(const tinyobj_opt::material_t& mtl);
    void createUBO(const tinyobj_opt::material_t& mtl);
    VulkanResource* tryLoadTexture(std::string file_path_str, const std::string& material_base_name, const char* init_dir);
    void loadTexture(std::string file_path_str, const std::string& material_base_name, const char* init_dir, const texture_type type);
    void textureLoadedCallback(void* data_ptr, void* user_data);

    struct texture_toggles_t
    {
        VkBool32 hasAlbedoMap{ VK_FALSE };
        VkBool32 hasAlphaMap{ VK_FALSE };
        VkBool32 hasSpecularMap{ VK_FALSE };
        VkBool32 hasBumpMap{ VK_FALSE };
        VkBool32 hasDisplacementMap{ VK_FALSE };
        VkBool32 hasNormalMap{ VK_FALSE };
        VkBool32 hasAmbientOcclusionMap{ VK_FALSE };
        VkBool32 hasMetallicMap{ VK_FALSE };
        VkBool32 hasRoughnessMap{ VK_FALSE };
        VkBool32 hasEmissiveMap{ VK_FALSE };
        VkBool32 materialLoading{ VK_TRUE };
    };

    bool opaque{ true };
    // used to indicate resource queued for loading
    texture_toggles_t textureTogglesCPU;
    // used to indiate resources loaded and resident on GPU: can be used for rendering
    texture_toggles_t textureTogglesGPU;
    material_parameters_t parameters;
    VulkanResource* paramsUbo{ nullptr };
    VulkanResource* albedoMap{ nullptr };
    VulkanResource* alphaMap{ nullptr };
    VulkanResource* specularMap{ nullptr };
    VulkanResource* bumpMap{ nullptr };
    VulkanResource* displacementMap{ nullptr };
    VulkanResource* normalMap{ nullptr };
    VulkanResource* ambientOcclusionMap{ nullptr };
    VulkanResource* metallicMap{ nullptr };
    VulkanResource* roughnessMap{ nullptr };
    VulkanResource* emissiveMap{ nullptr };

    friend void materialTextureLoadedCallback(void*, void*, void*);

};

#endif //!TEST_FIXTURES_MATERIAL_HPP