#ifndef TEST_FIXTURES_MATERIAL_HPP
#define TEST_FIXTURES_MATERIAL_HPP
#include "ForwardDecl.hpp"
#include "ResourceTypes.hpp"
#include "MaterialStructures.hpp"
#include <vulkan/vulkan_core.h>
#include <atomic>

namespace tinyobj_opt
{
    class material_t;
}

class DescriptorBinder;
class Descriptor;

/*
    This is the actual data tied to a single material, but isn't presented to clients. They'll access these through the usage of 
    material instances instead.
*/
class Material
{
    Material(const Material&) = delete;
    Material& operator=(const Material&) = delete;

public:

    Material() = default;
    Material(MaterialCreateInfo createInfo);
    Material(Material&& other) noexcept;
    Material& operator=(Material&& other) noexcept;
    ~Material();

    void PopulateDescriptor(Descriptor& descr) const;
    void Bind(VkCommandBuffer cmd, const VkPipelineLayout layout, DescriptorBinder& binder);
    bool Opaque() const noexcept;

    static Material CreateDebugMaterial();


private:

    void createUBO(const tinyobj_opt::material_t& mtl);
    void loadTexture(const char* file_path_str, const char* material_base_name, const char* init_dir, const texture_type type);
    void textureLoadedCallback(void* data_ptr, void* user_data);


    bool opaque{ true };

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