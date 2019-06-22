#include "MaterialCache.hpp"
#include "ResourceContext.hpp"
#include "ResourceLoader.hpp"
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
    template<texture_type tex_type_e>
    constexpr static texture_type TEXTURE_TYPE_VAL = tex_type_e;
    // force initialization now because we use these by address, not value, so we need a fixed address
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

    struct loaded_texture_data_t
    {
        loaded_texture_data_t(const char* fname) : bitmap(fname) {}
        mango::Bitmap bitmap;
        VulkanResource* resource{ nullptr };
    };

    float extract_valid_material_param(const float input);
    glm::vec3 extract_valid_material_param(const glm::vec3& in) noexcept;
    material_parameters_t from_tinyobj_material(const tinyobj_opt::material_t& mtl) noexcept;
    VulkanResource* createUBO(const char* name, const material_parameters_t& params); 
    std::string FindDebugTexture();

}

MaterialPool::material_cache_page_t::material_cache_page_t() noexcept
{
    // filling with this value so that we reserve some memory for each entry ahead of time
    namesArray.fill(std::string{ "unused_material_name" });
    parameterUBOs.fill(nullptr);
    albedoMaps.fill(nullptr);
    alphaMaps.fill(nullptr);
    specularMaps.fill(nullptr);
    bumpMaps.fill(nullptr);
    displacementMaps.fill(nullptr);
    normalMaps.fill(nullptr);
    aoMaps.fill(nullptr);
    metallicMaps.fill(nullptr);
    roughnessMaps.fill(nullptr);
    emissiveMaps.fill(nullptr);
}

MaterialPool& MaterialPool::Get() noexcept
{
    static MaterialPool pool;
    return pool;
}

MaterialInstance* MaterialPool::LoadTinyobjMaterial(const tinyobj_opt::material_t& mtl, const char* material_dir)
{
    return nullptr;
}

namespace
{

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

}