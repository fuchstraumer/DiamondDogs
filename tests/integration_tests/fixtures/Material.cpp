#include "Material.hpp"
#include "DescriptorBinder.hpp"
#include "LogicalDevice.hpp"
#include "ResourceContext.hpp"
#include "ResourceLoader.hpp"
#include "RenderingContext.hpp"
#include "Descriptor.hpp"
#include <array>
#include <condition_variable>
#include <experimental/filesystem>
#include <mutex>
#include <unordered_set>
#include <algorithm>
#include <execution>
#pragma warning(push, 1)
#include "easylogging++.h"
#include "mango/include/mango/mango.hpp"
#pragma warning(pop)

namespace stdfs = std::experimental::filesystem;

template<texture_type tex_type_e>
constexpr static texture_type TEXTURE_TYPE_VAL = tex_type_e;
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

static std::mutex debugCreationMutex;
static std::condition_variable cVar;
static VulkanResource* failedLoadDebugTexture{ nullptr };
static VulkanResource* loadingMaterialTexture{ nullptr };

std::string FindDebugTexture()
{
    namespace stdfs = std::experimental::filesystem;
    static const std::string TextureFname("FailedTexLoad.png");

    auto case_insensitive_comparison = [](const std::string & fname, const std::string & curr_entry)->bool
    {

        return std::equal(std::execution::par_unseq, fname.cbegin(), fname.cend(), curr_entry.cbegin(), curr_entry.cend(), [](const char a, const char b)
            {
                return std::tolower(a) == std::tolower(b);
            });
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

struct loaded_texture_data_t
{
    loaded_texture_data_t(const char* fname) : bitmap(fname) {}
    mango::Bitmap bitmap;
    VulkanResource* resource{ nullptr };
};

void* loadImageDataFn(const char* fname, void* user_data)
{
    namespace fs = std::experimental::filesystem;
    using namespace mango;
    auto& rsrc_context = ResourceContext::Get();
    const auto* device = RenderingContext::Get().Device();
    const uint32_t graphics_queue_idx = device->QueueFamilyIndices().Graphics;

    stdfs::path file_path(fname);
    std::string fileStr(fname);

    if (!stdfs::exists(file_path))
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

void destroyLoadedImageDataFn(void* object, void* user_data)
{
    loaded_texture_data_t* ptr = reinterpret_cast<loaded_texture_data_t*>(object);
    auto& rsrc_context = ResourceContext::Get();
    rsrc_context.DestroyResource(ptr->resource);
    delete ptr;
}

void materialTextureLoadedCallback(void* instance, void* data_ptr, void* user_data)
{
    reinterpret_cast<Material*>(instance)->textureLoadedCallback(data_ptr, user_data);
}

float extract_valid_material_param(const float input)
{

    if (std::isnan(input))
    {
        return 0.0f;
    }

    // we can get weird values from the importer, so check for reasonable quantities here
    if (input < -1.0e8f || input > 1.0e8f)
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

Material::Material(const tinyobj_opt::material_t& mtl, const char* mtl_dir)
{
    auto& resource_loader = ResourceLoader::GetResourceLoader();
    resource_loader.Subscribe("MANGO", loadImageDataFn, destroyLoadedImageDataFn);

    setParameters(mtl);
    createUBO(mtl);

    if (!mtl.ambient_texname.empty())
    {
        loadTexture(mtl.ambient_texname, mtl.name, mtl_dir, AMBIENT_TEXTURE_TYPE);
    }

    if (!mtl.diffuse_texname.empty())
    {
        loadTexture(mtl.diffuse_texname, mtl.name, mtl_dir, DIFFUSE_TEXTURE_TYPE);
    }

    if (!mtl.alpha_texname.empty())
    {
        opaque = false;
        loadTexture(mtl.alpha_texname, mtl.name, mtl_dir, ALPHA_TEXTURE_TYPE);
    }

    if (!mtl.specular_texname.empty())
    {
        loadTexture(mtl.specular_texname, mtl.name, mtl_dir, SPECULAR_TEXTURE_TYPE);
    }

    if (!mtl.bump_texname.empty())
    {
        loadTexture(mtl.bump_texname, mtl.name, mtl_dir, BUMP_TEXTURE_TYPE);
    }

    if (!mtl.displacement_texname.empty())
    {
        loadTexture(mtl.displacement_texname, mtl.name, mtl_dir, DISPLACEMENT_TEXTURE_TYPE);
    }

    if (!mtl.normal_texname.empty())
    {
        loadTexture(mtl.normal_texname, mtl.name, mtl_dir, NORMAL_TEXTURE_TYPE);
    }

    if (!mtl.metallic_texname.empty())
    {
        loadTexture(mtl.metallic_texname, mtl.name, mtl_dir, METALLIC_TEXTURE_TYPE);
    }

    if (!mtl.roughness_texname.empty())
    {
        loadTexture(mtl.roughness_texname, mtl.name, mtl_dir, ROUGHNESS_TEXTURE_TYPE);
    }

}

Material::Material(Material&& other) noexcept : opaque(other.opaque), textureTogglesCPU(std::move(other.textureTogglesCPU)), parameters(std::move(other.parameters)), paramsUbo(std::move(other.paramsUbo)),
    albedoMap(std::move(other.albedoMap)), alphaMap(std::move(other.alphaMap)), specularMap(std::move(other.specularMap)), bumpMap(std::move(other.bumpMap)), displacementMap(std::move(other.displacementMap)),
    normalMap(std::move(other.normalMap)), ambientOcclusionMap(std::move(other.ambientOcclusionMap)), metallicMap(std::move(other.metallicMap)), textureTogglesGPU(std::move(other.textureTogglesGPU)),
    roughnessMap(std::move(other.roughnessMap)), emissiveMap(std::move(other.emissiveMap))
{
    other.paramsUbo = nullptr;
    other.albedoMap = nullptr;
    other.alphaMap = nullptr;
    other.specularMap = nullptr;
    other.bumpMap = nullptr;
    other.displacementMap = nullptr;
    other.normalMap = nullptr;
    other.ambientOcclusionMap = nullptr;
    other.metallicMap = nullptr;
    other.roughnessMap = nullptr;
    other.emissiveMap = nullptr;
}

Material& Material::operator=(Material&& other) noexcept
{
    opaque = other.opaque;
    // move any data that's worth moving
    textureTogglesCPU = std::move(other.textureTogglesCPU);
    textureTogglesGPU = std::move(other.textureTogglesGPU);
    parameters = std::move(other.parameters);
    // copy the pointers because they're probably not worth doing more over
    paramsUbo = other.paramsUbo;
    albedoMap = other.albedoMap;
    alphaMap = std::move(other.alphaMap);
    specularMap = std::move(other.specularMap);
    bumpMap = other.bumpMap;
    displacementMap = other.displacementMap;
    normalMap = other.normalMap;
    ambientOcclusionMap = other.ambientOcclusionMap;
    metallicMap = other.metallicMap;
    roughnessMap = other.roughnessMap;
    emissiveMap = other.emissiveMap;
    // zero other handles so we don't try to destroy them
    other.paramsUbo = nullptr;
    other.albedoMap = nullptr;
    other.alphaMap = nullptr;
    other.specularMap = nullptr;
    other.bumpMap = nullptr;
    other.displacementMap = nullptr;
    other.normalMap = nullptr;
    other.ambientOcclusionMap = nullptr;
    other.metallicMap = nullptr;
    other.roughnessMap = nullptr;
    other.emissiveMap = nullptr;
    return *this;
}

Material::~Material()
{
    auto& rsrc_context = ResourceContext::Get();

    if (paramsUbo)
    {
        rsrc_context.DestroyResource(paramsUbo);
    }

    if (albedoMap)
    {
        rsrc_context.DestroyResource(albedoMap);
        textureCounter.AlbedoMaps.fetch_sub(1);
    }

    if (alphaMap)
    {
        rsrc_context.DestroyResource(alphaMap);
        textureCounter.AlphaMaps.fetch_sub(1);
    }

    if (specularMap)
    {
        rsrc_context.DestroyResource(specularMap);
        textureCounter.SpecularMaps.fetch_sub(1);
    }

    if (bumpMap)
    {
        rsrc_context.DestroyResource(bumpMap);
        textureCounter.BumpMaps.fetch_sub(1);
    }

    if (displacementMap)
    {
        rsrc_context.DestroyResource(displacementMap);
        textureCounter.DisplacementMaps.fetch_sub(1);
    }

    if (normalMap)
    {
        rsrc_context.DestroyResource(normalMap);
        textureCounter.NormalMaps.fetch_sub(1);
    }

    if (ambientOcclusionMap)
    {
        rsrc_context.DestroyResource(ambientOcclusionMap);
        textureCounter.AoMaps.fetch_sub(1);
    }

    if (metallicMap)
    {
        rsrc_context.DestroyResource(metallicMap);
        textureCounter.MetallicMaps.fetch_sub(1);
    }

    if (roughnessMap)
    {
        rsrc_context.DestroyResource(roughnessMap);
        textureCounter.RoughnessMaps.fetch_sub(1);
    }

    if (emissiveMap)
    {
        rsrc_context.DestroyResource(emissiveMap);
        textureCounter.EmissiveMaps.fetch_sub(1);
    }

}

void Material::PopulateDescriptor(Descriptor& descr) const
{
    descr.BindResource("MaterialParameters", VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, paramsUbo);
    descr.BindResource("AlbedoMap", VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, albedoMap);
    descr.BindResource("AlphaMap", VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, alphaMap);
    descr.BindResource("SpecularMap", VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, specularMap);
    descr.BindResource("BumpMap", VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, bumpMap);
    descr.BindResource("DisplacementMap", VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, displacementMap);
    descr.BindResource("NormalMap", VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, normalMap);
    descr.BindResource("AmbientOcclusionMap", VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, ambientOcclusionMap);
    descr.BindResource("MetallicMap", VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, metallicMap);
    descr.BindResource("RoughnessMap", VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, roughnessMap);
    descr.BindResource("EmissiveMap", VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, emissiveMap);
}

void Material::Bind(VkCommandBuffer cmd, const VkPipelineLayout layout, DescriptorBinder& binder)
{

    auto& rsrc_context = ResourceContext::Get();

    {
        // this item is CPU side: don't need to check for loaded status
        binder.BindResource("Material", "MaterialParameters", VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, paramsUbo);
    }

    if (textureTogglesCPU.materialLoading)
    {
        constexpr static texture_toggles_t debugToggles{
            VK_TRUE,
            VK_FALSE,
            VK_FALSE,
            VK_FALSE,
            VK_FALSE,
            VK_FALSE,
            VK_FALSE,
            VK_FALSE,
            VK_FALSE,
            VK_FALSE,
            VK_FALSE
        };
        vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0u, sizeof(texture_toggles_t), &debugToggles);
        return; // leave default items bound
    }

    if (textureTogglesCPU.hasAlbedoMap && !rsrc_context.ResourceInTransferQueue(albedoMap))
    {
        binder.BindResource("Material", "AlbedoMap", VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, albedoMap);
        textureTogglesGPU.hasAlbedoMap = VK_TRUE;
        textureTogglesGPU.materialLoading = VK_FALSE;
    }
    
    if (textureTogglesCPU.hasAlphaMap && !rsrc_context.ResourceInTransferQueue(alphaMap))
    {
        binder.BindResource("Material", "AlphaMap", VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, alphaMap);
        textureTogglesGPU.hasAlphaMap = VK_TRUE;
        textureTogglesGPU.materialLoading = VK_FALSE;
    }

    if (textureTogglesCPU.hasSpecularMap && !rsrc_context.ResourceInTransferQueue(specularMap))
    {
        binder.BindResource("Material", "SpecularMap", VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, specularMap);
        textureTogglesGPU.hasSpecularMap = VK_TRUE;
        textureTogglesGPU.materialLoading = VK_FALSE;
    }

    if (textureTogglesCPU.hasBumpMap && !rsrc_context.ResourceInTransferQueue(bumpMap))
    {
        binder.BindResource("Material", "NormalMap", VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, bumpMap);
        textureTogglesGPU.hasBumpMap = VK_TRUE;
        textureTogglesGPU.materialLoading = VK_FALSE;
    }

    if (textureTogglesCPU.hasDisplacementMap && !rsrc_context.ResourceInTransferQueue(displacementMap))
    {
        binder.BindResource("Material", "DisplacementMap", VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, displacementMap);
        textureTogglesGPU.hasDisplacementMap = VK_TRUE;
        textureTogglesGPU.materialLoading = VK_FALSE;
    }

    if (textureTogglesCPU.hasNormalMap && !rsrc_context.ResourceInTransferQueue(normalMap))
    {
        binder.BindResource("Material", "NormalMap", VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, normalMap);
        textureTogglesGPU.hasNormalMap = VK_TRUE;
        textureTogglesGPU.materialLoading = VK_FALSE;
    }

    if (textureTogglesCPU.hasAmbientOcclusionMap && !rsrc_context.ResourceInTransferQueue(ambientOcclusionMap))
    {
        binder.BindResource("Material", "AmbientOcclusionMap", VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, ambientOcclusionMap);
        textureTogglesGPU.hasAmbientOcclusionMap = VK_TRUE;
        textureTogglesGPU.materialLoading = VK_FALSE;
    }

    if (textureTogglesCPU.hasMetallicMap && !rsrc_context.ResourceInTransferQueue(metallicMap))
    {
        binder.BindResource("Material", "MetallicMap", VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, metallicMap);
        textureTogglesGPU.hasMetallicMap = VK_TRUE;
        textureTogglesGPU.materialLoading = VK_FALSE;
    }

    if (textureTogglesCPU.hasRoughnessMap && !rsrc_context.ResourceInTransferQueue(roughnessMap))
    {
        binder.BindResource("Material", "RoughnessMap", VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, roughnessMap);
        textureTogglesGPU.hasRoughnessMap = VK_TRUE;
        textureTogglesGPU.materialLoading = VK_FALSE;
    }

    if (textureTogglesCPU.hasEmissiveMap && !rsrc_context.ResourceInTransferQueue(emissiveMap))
    {
        binder.BindResource("Material", "EmissiveMap", VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, emissiveMap);
        textureTogglesGPU.hasEmissiveMap = VK_TRUE;
        textureTogglesGPU.materialLoading = VK_FALSE;
    }

    // push GPU-side toggles into pipe
    vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0u, sizeof(texture_toggles_t), &textureTogglesGPU);
    assert(!(textureTogglesGPU.hasNormalMap && textureTogglesGPU.hasBumpMap));
    binder.Update();
    binder.Bind(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS);

}

bool Material::Opaque() const noexcept
{
    return opaque;
}

Material Material::CreateDebugMaterial()
{
    Material result;
    result.parameters.ambient = glm::vec3(0.1f, 0.1f, 0.1f);
    result.parameters.diffuse = glm::vec3(1.0f, 1.0f, 1.0f); // just 1.0 of whatever diffuse is
    tinyobj_opt::material_t debug_material;
    debug_material.name = "DEBUG_MATERIAL";
    result.createUBO(debug_material);
    static const std::string debugTexturePath = FindDebugTexture();
    void* failedTextureData = loadImageDataFn(debugTexturePath.c_str(), nullptr);
    failedLoadDebugTexture = reinterpret_cast<loaded_texture_data_t*>(failedTextureData)->resource;
    // for descriptor reasons, set all of these
    result.textureLoadedCallback(failedTextureData, (void*)&AMBIENT_TEXTURE_TYPE);
    result.textureLoadedCallback(failedTextureData, (void*)&DIFFUSE_TEXTURE_TYPE);
    result.textureLoadedCallback(failedTextureData, (void*)&SPECULAR_TEXTURE_TYPE);
    result.textureLoadedCallback(failedTextureData, (void*)&BUMP_TEXTURE_TYPE);
    result.textureLoadedCallback(failedTextureData, (void*)&DISPLACEMENT_TEXTURE_TYPE);
    result.textureLoadedCallback(failedTextureData, (void*)&ALPHA_TEXTURE_TYPE);
    result.textureLoadedCallback(failedTextureData, (void*)&ROUGHNESS_TEXTURE_TYPE);
    result.textureLoadedCallback(failedTextureData, (void*)&METALLIC_TEXTURE_TYPE);
    result.textureLoadedCallback(failedTextureData, (void*)&EMISSIVE_TEXTURE_TYPE);
    result.textureLoadedCallback(failedTextureData, (void*)&NORMAL_TEXTURE_TYPE);
    // ... but then clear the toggles so they don't actually get used in the shader: just have data where descriptor wants
    result.textureTogglesCPU = texture_toggles_t{};
    result.textureTogglesGPU = texture_toggles_t{};
    result.textureTogglesCPU.hasAlbedoMap = VK_TRUE;

    loaded_texture_data_t* loaded_data = reinterpret_cast<loaded_texture_data_t*>(failedTextureData);
    delete loaded_data;
    return std::move(result);
}

void Material::setParameters(const tinyobj_opt::material_t& mtl)
{
    parameters.ambient = extract_valid_material_param(glm::vec3(mtl.ambient[0], mtl.ambient[1], mtl.ambient[2]));
    parameters.diffuse = extract_valid_material_param(glm::vec3(mtl.diffuse[0], mtl.diffuse[1], mtl.diffuse[2]));
    parameters.specular = extract_valid_material_param(glm::vec3(mtl.specular[0], mtl.specular[1], mtl.specular[2]));
    parameters.transmittance = extract_valid_material_param(glm::vec3(mtl.transmittance[0], mtl.transmittance[1], mtl.transmittance[2]));
    parameters.emission = extract_valid_material_param(glm::vec3(mtl.emission[0], mtl.emission[1], mtl.emission[2]));
    parameters.shininess = extract_valid_material_param(mtl.shininess);
    parameters.ior = extract_valid_material_param(mtl.ior);
    parameters.dissolve = extract_valid_material_param(mtl.dissolve);
    parameters.illum = static_cast<int32_t>(mtl.illum);
    parameters.roughness = extract_valid_material_param(mtl.roughness);
    parameters.metallic = extract_valid_material_param(mtl.metallic);
    parameters.sheen = extract_valid_material_param(mtl.sheen);
    parameters.clearcoat_thickness = extract_valid_material_param(mtl.clearcoat_thickness);
    parameters.clearcoat_roughness = extract_valid_material_param(mtl.clearcoat_roughness);
    parameters.anisotropy = extract_valid_material_param(mtl.anisotropy);
    parameters.anisotropy_rotation = extract_valid_material_param(mtl.anisotropy_rotation);
}

void Material::createUBO(const tinyobj_opt::material_t& mtl)
{
    constexpr static VkBufferCreateInfo ubo_create_info{
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        nullptr,
        0,
        sizeof(parameters_t),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_SHARING_MODE_EXCLUSIVE,
        0u,
        nullptr
    };

    auto& rsrc_context = ResourceContext::Get();
    const std::string ubo_name = mtl.name + "_UBO";
    const gpu_resource_data_t ubo_data{
        &parameters, sizeof(parameters_t), 0u, VK_QUEUE_FAMILY_IGNORED
    };

    paramsUbo = rsrc_context.CreateBuffer(&ubo_create_info, nullptr, 1, &ubo_data, resource_usage::CPU_ONLY, ResourceCreateMemoryStrategyMinFragmentation | ResourceCreateUserDataAsString, (void*)ubo_name.c_str());
}

void Material::loadTexture(std::string file_path_str, const std::string& material_base_name, const char* init_dir, const texture_type type)
{

    const texture_type* type_ptr = nullptr;

    switch (type)
    {
    case texture_type::Ambient:
        type_ptr = &AMBIENT_TEXTURE_TYPE;
        break;
    case texture_type::Diffuse:
        type_ptr = &DIFFUSE_TEXTURE_TYPE;
        break;
    case texture_type::Specular:
        type_ptr = &SPECULAR_TEXTURE_TYPE;
        break;
    case texture_type::Bump:
        type_ptr = &BUMP_TEXTURE_TYPE;
        break;
    case texture_type::Displacement:
        type_ptr = &DISPLACEMENT_TEXTURE_TYPE;
        break;
    case texture_type::Alpha:
        type_ptr = &ALPHA_TEXTURE_TYPE;
        break;
    case texture_type::Roughness:
        type_ptr = &ROUGHNESS_TEXTURE_TYPE;
        break;
    case texture_type::Metallic:
        type_ptr = &METALLIC_TEXTURE_TYPE;
        break;
    case texture_type::Emissive:
        type_ptr = &EMISSIVE_TEXTURE_TYPE;
        break;
    case texture_type::Normal:
        type_ptr = &NORMAL_TEXTURE_TYPE;
        break;
    }

    auto& loader = ResourceLoader::GetResourceLoader();
    loader.Load("MANGO", file_path_str.c_str(), init_dir, (void*)this, materialTextureLoadedCallback, (void*)type_ptr);

}

void Material::textureLoadedCallback(void* data_ptr, void* user_data)
{
    // called when data is loaded from disk and has been queued for transfer
    const loaded_texture_data_t* texture_data = reinterpret_cast<loaded_texture_data_t*>(data_ptr);
    const texture_type tex_type = *reinterpret_cast<texture_type*>(user_data);
    switch (tex_type)
    {
    case texture_type::Ambient:
        textureTogglesCPU.hasAmbientOcclusionMap = VK_TRUE;
        ambientOcclusionMap = texture_data->resource;
        textureCounter.AoMaps.fetch_add(1u);
        break;
    case texture_type::Diffuse:
        textureTogglesCPU.hasAlbedoMap = VK_TRUE;
        albedoMap = texture_data->resource;
        textureCounter.AlbedoMaps.fetch_add(1u);
        break;
    case texture_type::Specular:
        textureTogglesCPU.hasSpecularMap = VK_TRUE;
        specularMap = texture_data->resource;
        textureCounter.SpecularMaps.fetch_add(1u);
        break;
    case texture_type::Bump:
        textureTogglesCPU.hasBumpMap = VK_TRUE;
        bumpMap = texture_data->resource;
        textureCounter.BumpMaps.fetch_add(1u);
        break;
    case texture_type::Displacement:
        textureTogglesCPU.hasDisplacementMap = VK_TRUE;
        displacementMap = texture_data->resource;
        textureCounter.DisplacementMaps.fetch_add(1u);
        break;
    case texture_type::Alpha:
        textureTogglesCPU.hasAlphaMap = VK_TRUE;
        alphaMap = texture_data->resource;
        textureCounter.AlphaMaps.fetch_add(1u);
        break;
    case texture_type::Roughness:
        textureTogglesCPU.hasRoughnessMap = VK_TRUE;
        roughnessMap = texture_data->resource;
        textureCounter.RoughnessMaps.fetch_add(1u);
        break;
    case texture_type::Metallic:
        textureTogglesCPU.hasMetallicMap = VK_TRUE;
        metallicMap = texture_data->resource;
        textureCounter.MetallicMaps.fetch_add(1u);
        break;
    case texture_type::Emissive:
        textureTogglesCPU.hasEmissiveMap = VK_TRUE;
        emissiveMap = texture_data->resource;
        break;
    case texture_type::Normal:
        textureTogglesCPU.hasNormalMap = VK_TRUE;
        normalMap = texture_data->resource;
        textureCounter.NormalMaps.fetch_add(1u);
        break;
    default:
        throw std::domain_error("Texture data's type was out of range!");
    }

    // at least one texture queued for loading. disable material loading status.
    textureTogglesCPU.materialLoading = VK_FALSE;

}
