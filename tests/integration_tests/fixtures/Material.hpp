#ifndef TEST_FIXTURES_MATERIAL_HPP
#define TEST_FIXTURES_MATERIAL_HPP
#include "ForwardDecl.hpp"
#include "ResourceTypes.hpp"
#include "glm/vec3.hpp"
#include "glm/vec4.hpp"
#pragma warning(push, 1)
#include "tinyobj_loader_opt.h"
#pragma warning(pop)
#include <vulkan/vulkan.h>
#include <atomic>

class DescriptorBinder;
class Descriptor;

enum class texture_type : uint8_t
{
    Invalid = 0u,
    Ambient,
    Diffuse,
    Specular,
    SpecularHighlight,
    Bump,
    Displacement,
    Alpha,
    Roughness,
    Metallic,
    Sheen,
    Emissive,
    Normal
};

class Material
{
    Material(const Material&) = delete;
    Material& operator=(const Material&) = delete;

public:

    struct material_texture_counter_t
    {
        std::atomic<uint32_t> AlbedoMaps{ 0u };
        std::atomic<uint32_t> AlphaMaps{ 0u };
        std::atomic<uint32_t> SpecularMaps{ 0u };
        std::atomic<uint32_t> BumpMaps{ 0u };
        std::atomic<uint32_t> DisplacementMaps{ 0u };
        std::atomic<uint32_t> NormalMaps{ 0u };
        std::atomic<uint32_t> AoMaps{ 0u };
        std::atomic<uint32_t> MetallicMaps{ 0u };
        std::atomic<uint32_t> RoughnessMaps{ 0u };
        std::atomic<uint32_t> EmissiveMaps{ 0u };
        bool Empty() const noexcept;
    };

    Material() = default;
    Material(const tinyobj_opt::material_t& mtl, const char* material_dir);
    Material(Material&& other) noexcept;
    Material& operator=(Material&& other) noexcept;
    ~Material();

    void PopulateDescriptor(Descriptor& descr) const;
    void Bind(VkCommandBuffer cmd, const VkPipelineLayout layout, DescriptorBinder& binder);
    bool Opaque() const noexcept;

    static Material CreateDebugMaterial();
    static const material_texture_counter_t& TextureCounter() noexcept;


private:

    static material_texture_counter_t textureCounter;
    void setParameters(const tinyobj_opt::material_t& mtl);
    void createUBO(const tinyobj_opt::material_t& mtl);
    VulkanResource* tryLoadTexture(std::string file_path_str, const std::string& material_base_name, const char* init_dir);
    void loadTexture(std::string file_path_str, const std::string& material_base_name, const char* init_dir, const texture_type type);
    void textureLoadedCallback(void* data_ptr, void* user_data);

    struct parameters_t
    {
        glm::vec3 ambient{ 0.0f,0.0f,0.0f };
        glm::vec3 diffuse{ 0.0f,0.0f,0.0f };
        glm::vec3 specular{ 0.0f,0.0f,0.0f };
        glm::vec3 transmittance{ 0.0f,0.0f,0.0f };
        glm::vec3 emission{ 0.0f,0.0f,0.0f };
        float shininess{ 0.01f };
        float ior{ 0.0f };
        float dissolve{ 0.0f };
        int32_t illum{ 0 };
        float roughness{ 0.1f };
        float metallic{ 0.0f };
        float sheen{ 0.0f };
        float clearcoat_thickness{ 0.0f };
        float clearcoat_roughness{ 0.0f };
        float anisotropy{ 0.0f };
        float anisotropy_rotation{ 0.0f };
    };

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
    parameters_t parameters;
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