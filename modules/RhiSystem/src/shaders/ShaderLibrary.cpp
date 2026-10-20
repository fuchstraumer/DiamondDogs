#include "ShaderLibrary.hpp"
#include "ShaderProgram.hpp"
#include "ShaderTypes.hpp"
#include "ShaderManifest.hpp"
#include "ShaderManifestIndex.hpp"
#include <algorithm>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <vulkan/vulkan_core.h>

namespace
{
    struct DescriptorSetBindings
    {
        std::vector<VkDescriptorSetLayoutBinding> Bindings;
    };

    [[nodiscard]] rhi::ShaderLibraryResult<std::vector<std::byte>> ReadManifestBytes(const std::filesystem::path& manifest_path);
    [[nodiscard]] std::string_view SpirvEntryPointName(std::span<const uint32_t> words) noexcept;
    [[nodiscard]] std::expected<VkDescriptorType, rhi::ShaderLibraryError> ToVkDescriptorType(const lodestone::manifest::ResolvedResource& resource) noexcept;
    [[nodiscard]] VkShaderStageFlags ToVkShaderStages(lodestone::ShaderStageKind stages) noexcept;
    [[nodiscard]] rhi::ShaderStageFlags ToRhiShaderStage(lodestone::ShaderStageKind stage) noexcept;
    [[nodiscard]] rhi::ShaderLibraryResult<std::vector<DescriptorSetBindings>> CollectSetBindings(const lodestone::manifest::MergedLayoutView& layout);
    void DestroyLayouts(VkDevice device, std::span<const rhi::DescriptorSetLayoutHandle> set_layouts, VkPipelineLayout pipeline_layout) noexcept;
}

namespace rhi
{
    struct ManifestProgramImpl
    {
        ~ManifestProgramImpl();

        DeviceHandle Device;
        std::unique_ptr<ShaderProgram> Program;
        std::vector<DescriptorSetLayoutHandle> SetLayouts;
        PipelineLayoutHandle PipelineLayout;
        std::array<uint32_t, 3> Workgroup{ 1u, 1u, 1u };
    };

    struct ShaderLibraryModule
    {
        // views returned by the environment keep a pointer to it, so it lives on the heap and never moves
        std::unique_ptr<lodestone::manifest::EnvironmentView> Environment;
        std::unique_ptr<lodestone::ManifestIndex> Index;
    };

    struct ShaderLibraryImpl
    {
        std::vector<std::byte> ManifestBytes;
        lodestone::manifest::BundleView Bundle;
        uint32_t ProfileIndex{ 0u };
        std::unordered_map<std::string, ShaderLibraryModule> Modules;

        [[nodiscard]] ShaderLibraryResult<const ShaderLibraryModule*> FindModule(std::string_view module_name) const;
    };

    ManifestProgramImpl::~ManifestProgramImpl()
    {
        // the shader objects reference the set layouts, so they go first
        Program.reset();
        DestroyLayouts(Device.As<VkDevice>(), SetLayouts, PipelineLayout.As<VkPipelineLayout>());
    }

    ShaderLibraryResult<const ShaderLibraryModule*> ShaderLibraryImpl::FindModule(std::string_view module_name) const
    {
        auto moduleIter = Modules.find(std::string{ module_name });
        if (moduleIter == Modules.end())
        {
            return std::unexpected(ShaderLibraryError::ModuleNotInManifest);
        }
        return &moduleIter->second;
    }

    std::string_view ToString(ShaderLibraryError error) noexcept
    {
        switch (error)
        {
            case ShaderLibraryError::Invalid: return "Invalid";
            case ShaderLibraryError::Success: return "Success";
            case ShaderLibraryError::ManifestFileUnreadable: return "ManifestFileUnreadable";
            case ShaderLibraryError::ManifestInvalid: return "ManifestInvalid";
            case ShaderLibraryError::ProfileNotInManifest: return "ProfileNotInManifest";
            case ShaderLibraryError::ProfileNotSpirv: return "ProfileNotSpirv";
            case ShaderLibraryError::ModuleNotInManifest: return "ModuleNotInManifest";
            case ShaderLibraryError::EnvironmentInvalid: return "EnvironmentInvalid";
            case ShaderLibraryError::EntryPointNotInModule: return "EntryPointNotInModule";
            case ShaderLibraryError::EntryPointNotInVariant: return "EntryPointNotInVariant";
            case ShaderLibraryError::NoEntryPointsRequested: return "NoEntryPointsRequested";
            case ShaderLibraryError::LayoutMergeFailed: return "LayoutMergeFailed";
            case ShaderLibraryError::UnsupportedResourcePlacement: return "UnsupportedResourcePlacement";
            case ShaderLibraryError::UnsupportedBindingKind: return "UnsupportedBindingKind";
            case ShaderLibraryError::DescriptorSetLayoutCreationFailed: return "DescriptorSetLayoutCreationFailed";
            case ShaderLibraryError::PipelineLayoutCreationFailed: return "PipelineLayoutCreationFailed";
            case ShaderLibraryError::ShaderProgramCreationFailed: return "ShaderProgramCreationFailed";
        }
        return "Unknown ShaderLibraryError";
    }

    ManifestProgram::ManifestProgram(std::unique_ptr<ManifestProgramImpl>&& _impl) noexcept :
        impl{ std::move(_impl) }
    {
    }

    ManifestProgram::~ManifestProgram() = default;
    ManifestProgram::ManifestProgram(ManifestProgram&& other) noexcept = default;
    ManifestProgram& ManifestProgram::operator=(ManifestProgram&& other) noexcept = default;

    ShaderProgram& ManifestProgram::Program() const noexcept
    {
        return *impl->Program;
    }

    PipelineLayoutHandle ManifestProgram::PipelineLayout() const noexcept
    {
        return impl->PipelineLayout;
    }

    std::span<const DescriptorSetLayoutHandle> ManifestProgram::SetLayouts() const noexcept
    {
        return impl->SetLayouts;
    }

    std::array<uint32_t, 3> ManifestProgram::Workgroup() const noexcept
    {
        return impl->Workgroup;
    }

    ShaderLibrary::ShaderLibrary(std::unique_ptr<ShaderLibraryImpl>&& _impl) noexcept :
        impl{ std::move(_impl) }
    {
    }

    ShaderLibrary::~ShaderLibrary() = default;

    ShaderLibraryResult<std::unique_ptr<ShaderLibrary>> ShaderLibrary::Open(
        const std::filesystem::path& manifest_path,
        std::string_view profile_name)
    {
        ShaderLibraryResult<std::vector<std::byte>> bytes = ReadManifestBytes(manifest_path);
        if (!bytes.has_value())
        {
            return std::unexpected(bytes.error());
        }

        auto libraryImpl = std::make_unique<ShaderLibraryImpl>();
        libraryImpl->ManifestBytes = std::move(*bytes);

        lodestone::manifest::ManifestResult<lodestone::manifest::BundleView> bundle =
            lodestone::manifest::BundleView::Open(libraryImpl->ManifestBytes);
        if (!bundle.has_value())
        {
            return std::unexpected(ShaderLibraryError::ManifestInvalid);
        }
        libraryImpl->Bundle = *bundle;

        const int32_t profileIndex = libraryImpl->Bundle.FindProfile(profile_name);
        if (profileIndex < 0)
        {
            return std::unexpected(ShaderLibraryError::ProfileNotInManifest);
        }
        std::span<const lodestone::manifest::Profile> profiles = libraryImpl->Bundle.Profiles();
        if (profiles[profileIndex].CodeFormat != lodestone::ShaderCodeFormat::Spirv)
        {
            return std::unexpected(ShaderLibraryError::ProfileNotSpirv);
        }
        libraryImpl->ProfileIndex = static_cast<uint32_t>(profileIndex);

        for (uint32_t moduleIndex = 0; moduleIndex < libraryImpl->Bundle.ModuleCount(); ++moduleIndex)
        {
            lodestone::manifest::ManifestResult<lodestone::manifest::EnvironmentView> environment =
                libraryImpl->Bundle.OpenEnvironment(libraryImpl->ProfileIndex, moduleIndex);
            if (!environment.has_value())
            {
                return std::unexpected(ShaderLibraryError::EnvironmentInvalid);
            }
            ShaderLibraryModule libraryModule;
            libraryModule.Environment = std::make_unique<lodestone::manifest::EnvironmentView>(*environment);
            libraryModule.Index = std::make_unique<lodestone::ManifestIndex>(*libraryModule.Environment);
            const std::string_view moduleName = libraryImpl->Bundle.Module(moduleIndex).Name();
            libraryImpl->Modules.emplace(std::string{ moduleName }, std::move(libraryModule));
        }

        return std::make_unique<ShaderLibrary>(std::move(libraryImpl));
    }

    ShaderLibraryResult<const lodestone::ManifestIndex*> ShaderLibrary::ModuleIndex(std::string_view module_name) const
    {
        ShaderLibraryResult<const ShaderLibraryModule*> libraryModule = impl->FindModule(module_name);
        if (!libraryModule.has_value())
        {
            return std::unexpected(libraryModule.error());
        }
        return (*libraryModule)->Index.get();
    }

    ShaderLibraryResult<std::unique_ptr<ManifestProgram>> ShaderLibrary::CreateProgram(
        DeviceHandle device,
        std::string_view module_name,
        lodestone::VariantKey variant,
        std::span<const std::string_view> entry_points) const
    {
        if (entry_points.empty())
        {
            return std::unexpected(ShaderLibraryError::NoEntryPointsRequested);
        }

        ShaderLibraryResult<const ShaderLibraryModule*> libraryModule = impl->FindModule(module_name);
        if (!libraryModule.has_value())
        {
            return std::unexpected(libraryModule.error());
        }
        const lodestone::manifest::EnvironmentView& environment = *(*libraryModule)->Environment;

        std::optional<lodestone::manifest::VariantView> variantView = environment.VariantByKey(variant);
        if (!variantView.has_value())
        {
            return std::unexpected(ShaderLibraryError::EntryPointNotInVariant);
        }

        std::vector<lodestone::manifest::EntryPointInstanceView> instances;
        instances.reserve(entry_points.size());
        for (const std::string_view entryPointName : entry_points)
        {
            lodestone::manifest::ManifestResult<uint32_t> entryPointIndex = environment.Module().FindEntryPointIndex(entryPointName);
            if (!entryPointIndex.has_value())
            {
                return std::unexpected(ShaderLibraryError::EntryPointNotInModule);
            }
            if (environment.FindEntryPointInstance(*entryPointIndex, variant) == nullptr)
            {
                return std::unexpected(ShaderLibraryError::EntryPointNotInVariant);
            }
            instances.push_back(variantView->EntryPoint(*entryPointIndex));
        }

        lodestone::manifest::ManifestResult<lodestone::manifest::MergedLayoutView> mergedLayout =
            lodestone::manifest::MergedLayoutView::Merge(instances);
        if (!mergedLayout.has_value())
        {
            return std::unexpected(ShaderLibraryError::LayoutMergeFailed);
        }

        ShaderLibraryResult<std::vector<DescriptorSetBindings>> setBindings = CollectSetBindings(*mergedLayout);
        if (!setBindings.has_value())
        {
            return std::unexpected(setBindings.error());
        }

        auto programImpl = std::make_unique<ManifestProgramImpl>();
        programImpl->Device = device;
        const VkDevice vkDevice = device.As<VkDevice>();

        for (const DescriptorSetBindings& set : *setBindings)
        {
            const VkDescriptorSetLayoutCreateInfo setLayoutInfo
            {
                VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                nullptr,
                0,
                static_cast<uint32_t>(set.Bindings.size()),
                set.Bindings.data()
            };
            VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
            if (vkCreateDescriptorSetLayout(vkDevice, &setLayoutInfo, nullptr, &setLayout) != VK_SUCCESS)
            {
                return std::unexpected(ShaderLibraryError::DescriptorSetLayoutCreationFailed);
            }
            DescriptorSetLayoutHandle& setLayoutHandle = programImpl->SetLayouts.emplace_back();
            setLayoutHandle.Set(setLayout);
        }

        std::vector<VkDescriptorSetLayout> vkSetLayouts;
        vkSetLayouts.reserve(programImpl->SetLayouts.size());
        for (const DescriptorSetLayoutHandle& setLayout : programImpl->SetLayouts)
        {
            vkSetLayouts.push_back(setLayout.As<VkDescriptorSetLayout>());
        }

        // todo: push constants. The cook reflects entry point `uniform` parameters, but how they appear in the
        // manifest is still an open Lodestone question (see todo.md), so cooked programs use none for now
        const VkPipelineLayoutCreateInfo pipelineLayoutInfo
        {
            VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            nullptr,
            0,
            static_cast<uint32_t>(vkSetLayouts.size()),
            vkSetLayouts.data(),
            0,
            nullptr
        };
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        if (vkCreatePipelineLayout(vkDevice, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS)
        {
            return std::unexpected(ShaderLibraryError::PipelineLayoutCreationFailed);
        }
        programImpl->PipelineLayout.Set(pipelineLayout);

        // ShaderBinaryOptions keeps spans and an entry point name, so this storage must outlive program creation
        std::vector<ShaderBinaryOptions> binaryOptions;
        binaryOptions.reserve(instances.size());
        for (const lodestone::manifest::EntryPointInstanceView& instance : instances)
        {
            // the cooked SPIR-V does not keep the Slang entry point name: Slang names every SPIR-V entry point
            // "main". Read the name from the code itself, so a change on the Slang side can't break us silently
            const std::span<const uint32_t> words = instance.SpirvWords();
            binaryOptions.emplace_back(
                words,
                ToRhiShaderStage(instance.Stage()),
                SpirvEntryPointName(words),
                std::span<PushConstantRange>{},
                std::span<DescriptorSetLayoutHandle>{ programImpl->SetLayouts });
        }

        const lodestone::WorkgroupSize workgroup = instances.front().Workgroup();
        programImpl->Workgroup = { workgroup.X, workgroup.Y, workgroup.Z };

        if (ShaderProgram::CreateFromBinary(device, binaryOptions, programImpl->Program).IsFailure())
        {
            return std::unexpected(ShaderLibraryError::ShaderProgramCreationFailed);
        }

        return std::make_unique<ManifestProgram>(std::move(programImpl));
    }

} // namespace rhi

namespace
{
    rhi::ShaderLibraryResult<std::vector<std::byte>> ReadManifestBytes(const std::filesystem::path& manifest_path)
    {
        std::ifstream manifestFile(manifest_path, std::ios::binary | std::ios::ate);
        if (!manifestFile.is_open())
        {
            return std::unexpected(rhi::ShaderLibraryError::ManifestFileUnreadable);
        }
        const std::streamsize fileSize = manifestFile.tellg();
        if (fileSize <= 0)
        {
            return std::unexpected(rhi::ShaderLibraryError::ManifestFileUnreadable);
        }
        std::vector<std::byte> bytes(static_cast<size_t>(fileSize));
        manifestFile.seekg(0, std::ios::beg);
        if (!manifestFile.read(reinterpret_cast<char*>(bytes.data()), fileSize))
        {
            return std::unexpected(rhi::ShaderLibraryError::ManifestFileUnreadable);
        }
        return bytes;
    }

    std::string_view SpirvEntryPointName(std::span<const uint32_t> words) noexcept
    {
        // SPIR-V: a 5 word header, then instructions whose first word is (word count << 16) | opcode.
        // OpEntryPoint (opcode 15) stores the execution model, the function id, then a null-terminated name
        constexpr uint32_t k_HeaderWordCount = 5u;
        constexpr uint32_t k_OpEntryPoint = 15u;
        constexpr uint32_t k_NameWordOffset = 3u;
        size_t position = k_HeaderWordCount;
        while (position < words.size())
        {
            const uint32_t opcode = words[position] & 0xFFFFu;
            const uint32_t wordCount = words[position] >> 16u;
            if (wordCount == 0u)
            {
                break;
            }
            if (opcode == k_OpEntryPoint && wordCount > k_NameWordOffset)
            {
                return std::string_view{ reinterpret_cast<const char*>(words.data() + position + k_NameWordOffset) };
            }
            position += wordCount;
        }
        return std::string_view{ "main" };
    }

    std::expected<VkDescriptorType, rhi::ShaderLibraryError> ToVkDescriptorType(const lodestone::manifest::ResolvedResource& resource) noexcept
    {
        const bool writable = resource.Access() != lodestone::ResourceAccess::ReadOnly;
        switch (resource.Kind())
        {
            case lodestone::BindingKind::Sampler: return VK_DESCRIPTOR_TYPE_SAMPLER;
            case lodestone::BindingKind::Texture: return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            case lodestone::BindingKind::UniformBuffer: return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            case lodestone::BindingKind::StorageBuffer: return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            case lodestone::BindingKind::TexelBuffer: return writable ? VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER : VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
            case lodestone::BindingKind::CombinedTextureSampler: return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            case lodestone::BindingKind::InputRenderTarget: return VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
            case lodestone::BindingKind::InlineUniform: return VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK;
            case lodestone::BindingKind::RayTracingAccelerationStructure: return VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
            case lodestone::BindingKind::StorageTexture: return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            default: return std::unexpected(rhi::ShaderLibraryError::UnsupportedBindingKind);
        }
    }

    VkShaderStageFlags ToVkShaderStages(lodestone::ShaderStageKind stages) noexcept
    {
        constexpr std::pair<lodestone::ShaderStageKind, VkShaderStageFlagBits> stageTable[]
        {
            { lodestone::ShaderStageKind::Vertex, VK_SHADER_STAGE_VERTEX_BIT },
            { lodestone::ShaderStageKind::Hull, VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT },
            { lodestone::ShaderStageKind::TessellationControl, VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT },
            { lodestone::ShaderStageKind::Domain, VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT },
            { lodestone::ShaderStageKind::TessellationEvaluation, VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT },
            { lodestone::ShaderStageKind::Geometry, VK_SHADER_STAGE_GEOMETRY_BIT },
            { lodestone::ShaderStageKind::Fragment, VK_SHADER_STAGE_FRAGMENT_BIT },
            { lodestone::ShaderStageKind::Compute, VK_SHADER_STAGE_COMPUTE_BIT },
            { lodestone::ShaderStageKind::RayGeneration, VK_SHADER_STAGE_RAYGEN_BIT_KHR },
            { lodestone::ShaderStageKind::Intersection, VK_SHADER_STAGE_INTERSECTION_BIT_KHR },
            { lodestone::ShaderStageKind::AnyHit, VK_SHADER_STAGE_ANY_HIT_BIT_KHR },
            { lodestone::ShaderStageKind::ClosestHit, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR },
            { lodestone::ShaderStageKind::Miss, VK_SHADER_STAGE_MISS_BIT_KHR },
            { lodestone::ShaderStageKind::Callable, VK_SHADER_STAGE_CALLABLE_BIT_KHR },
            { lodestone::ShaderStageKind::Mesh, VK_SHADER_STAGE_MESH_BIT_EXT },
            { lodestone::ShaderStageKind::Amplification, VK_SHADER_STAGE_TASK_BIT_EXT },
            { lodestone::ShaderStageKind::Task, VK_SHADER_STAGE_TASK_BIT_EXT },
        };
        VkShaderStageFlags result = 0;
        for (const auto& [stage, vkStage] : stageTable)
        {
            if ((static_cast<uint32_t>(stages) & static_cast<uint32_t>(stage)) != 0u)
            {
                result |= vkStage;
            }
        }
        return result;
    }

    rhi::ShaderStageFlags ToRhiShaderStage(lodestone::ShaderStageKind stage) noexcept
    {
        switch (stage)
        {
            case lodestone::ShaderStageKind::Vertex: return rhi::ShaderStageFlags::Vertex;
            case lodestone::ShaderStageKind::Hull: return rhi::ShaderStageFlags::TesselationControl;
            case lodestone::ShaderStageKind::TessellationControl: return rhi::ShaderStageFlags::TesselationControl;
            case lodestone::ShaderStageKind::Domain: return rhi::ShaderStageFlags::TesselationEvaluation;
            case lodestone::ShaderStageKind::TessellationEvaluation: return rhi::ShaderStageFlags::TesselationEvaluation;
            case lodestone::ShaderStageKind::Geometry: return rhi::ShaderStageFlags::Geometry;
            case lodestone::ShaderStageKind::Fragment: return rhi::ShaderStageFlags::Fragment;
            case lodestone::ShaderStageKind::Compute: return rhi::ShaderStageFlags::Compute;
            case lodestone::ShaderStageKind::RayGeneration: return rhi::ShaderStageFlags::RayGeneration;
            case lodestone::ShaderStageKind::Intersection: return rhi::ShaderStageFlags::Intersection;
            case lodestone::ShaderStageKind::AnyHit: return rhi::ShaderStageFlags::AnyHit;
            case lodestone::ShaderStageKind::ClosestHit: return rhi::ShaderStageFlags::ClosestHit;
            case lodestone::ShaderStageKind::Miss: return rhi::ShaderStageFlags::Miss;
            case lodestone::ShaderStageKind::Callable: return rhi::ShaderStageFlags::Callable;
            case lodestone::ShaderStageKind::Mesh: return rhi::ShaderStageFlags::Mesh;
            case lodestone::ShaderStageKind::Amplification: return rhi::ShaderStageFlags::Task;
            case lodestone::ShaderStageKind::Task: return rhi::ShaderStageFlags::Task;
            default: return rhi::ShaderStageFlags::None;
        }
    }

    rhi::ShaderLibraryResult<std::vector<DescriptorSetBindings>> CollectSetBindings(const lodestone::manifest::MergedLayoutView& layout)
    {
        std::vector<DescriptorSetBindings> sets;
        for (const lodestone::manifest::MergedResource& merged : layout)
        {
            const lodestone::manifest::ResolvedResource& resource = merged.Resource;
            // the merge already rejects every placement that isn't Bound, so this only guards a future
            // Lodestone that lifts that restriction before this renderer handles the new placements
            if (resource.Placement() != lodestone::PlacementKind::Bound)
            {
                return std::unexpected(rhi::ShaderLibraryError::UnsupportedResourcePlacement);
            }
            std::expected<VkDescriptorType, rhi::ShaderLibraryError> descriptorType = ToVkDescriptorType(resource);
            if (!descriptorType.has_value())
            {
                return std::unexpected(descriptorType.error());
            }
            const lodestone::manifest::PlacementPayload placement = resource.PlacementValue();
            const uint32_t setIndex = placement.Word0;
            if (sets.size() <= setIndex)
            {
                sets.resize(setIndex + 1u);
            }
            sets[setIndex].Bindings.push_back(VkDescriptorSetLayoutBinding
            {
                placement.Word1,
                *descriptorType,
                std::max(resource.ArrayCount(), 1u),
                ToVkShaderStages(merged.Stages),
                nullptr
            });
        }
        return sets;
    }

    void DestroyLayouts(VkDevice device, std::span<const rhi::DescriptorSetLayoutHandle> set_layouts, VkPipelineLayout pipeline_layout) noexcept
    {
        if (pipeline_layout != VK_NULL_HANDLE)
        {
            vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
        }
        for (const rhi::DescriptorSetLayoutHandle& setLayout : set_layouts)
        {
            vkDestroyDescriptorSetLayout(device, setLayout.As<VkDescriptorSetLayout>(), nullptr);
        }
    }
}
