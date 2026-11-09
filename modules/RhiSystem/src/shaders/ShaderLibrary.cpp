#include "ShaderLibrary.hpp"
#include "ShaderBundle.hpp"
#include "RhiFlags.hpp"
#include "RhiHandle.hpp"
#include "RhiTypes.hpp"
#include "ShaderLibraryTypes.hpp"
#include "ShaderManifestFormat.hpp"
#include "ShaderManifestLayout.hpp"
#include "ShaderProgram.hpp"
#include "ShaderTypes.hpp"
#include "ShaderManifest.hpp"
#include "ShaderManifestIndex.hpp"
#include "VariantKey.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <vulkan/vulkan_core.h>

namespace
{
    struct DescriptorSetBindings
    {
        std::vector<VkDescriptorSetLayoutBinding> Bindings;
    };

    [[nodiscard]] rhi::ShaderLibraryError ToShaderLibraryError(rhi::ShaderBundleError error) noexcept;
    [[nodiscard]] std::expected<VkDescriptorType, rhi::ShaderLibraryError> ToVkDescriptorType(const lodestone::manifest::ResolvedResource& resource) noexcept;
    [[nodiscard]] VkShaderStageFlags ToVkShaderStages(lodestone::ShaderStageKind stages) noexcept;
    [[nodiscard]] rhi::ShaderStageFlags ToRhiShaderStage(lodestone::ShaderStageKind stage) noexcept;
    [[nodiscard]] rhi::ShaderStageFlags ToRhiShaderStages(lodestone::ShaderStageKind stages) noexcept;
    [[nodiscard]] rhi::ShaderLibraryResult<std::vector<DescriptorSetBindings>> CollectSetBindings(const lodestone::manifest::MergedLayoutView& layout);
    void DestroyLayouts(VkDevice device, std::span<const rhi::DescriptorSetLayoutHandle> set_layouts, VkPipelineLayout pipeline_layout) noexcept;
}

namespace rhi
{
    struct ManifestProgramImpl
    {
        ManifestProgramImpl() noexcept = default;
        ~ManifestProgramImpl();
        ManifestProgramImpl(const ManifestProgramImpl&) = delete;
        ManifestProgramImpl& operator=(const ManifestProgramImpl&) = delete;
        ManifestProgramImpl(ManifestProgramImpl&&) noexcept = default;
        ManifestProgramImpl& operator=(ManifestProgramImpl&&) noexcept = default;

        DeviceHandle Device;
        std::unique_ptr<ShaderProgram> Program;
        std::vector<DescriptorSetLayoutHandle> SetLayouts;
        PipelineLayoutHandle PipelineLayout;
        std::array<uint32_t, 3> Workgroup{ 1u, 1u, 1u };
        std::optional<PushConstantRange> PushConstants;
    };

    // The device-free half (bytes, environments, indices) is the bundle. The library adds the Vulkan objects.
    struct ShaderLibraryImpl
    {
        explicit ShaderLibraryImpl(ShaderBundle&& bundle) noexcept :
            Bundle{ std::move(bundle) }
        {
        }

        ShaderBundle Bundle;
    };

    ManifestProgramImpl::~ManifestProgramImpl()
    {
        // the shader objects reference the set layouts, so they go first
        Program.reset();
        DestroyLayouts(Device.As<VkDevice>(), SetLayouts, PipelineLayout.As<VkPipelineLayout>());
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

    std::optional<PushConstantRange> ManifestProgram::PushConstants() const noexcept
    {
        return impl->PushConstants;
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
        ShaderBundleResult<ShaderBundle> bundle = ShaderBundle::Open(manifest_path, profile_name);

        if (!bundle.has_value())
        {
            return std::unexpected(ToShaderLibraryError(bundle.error()));
        }

        // the bundle reads any profile; this library creates Vulkan objects, so it needs SPIR-V
        if (bundle->Profile().CodeFormat != lodestone::ShaderCodeFormat::Spirv)
        {
            return std::unexpected(ShaderLibraryError::ProfileNotSpirv);
        }

        return std::make_unique<ShaderLibrary>(std::make_unique<ShaderLibraryImpl>(std::move(*bundle)));
    }

    const ShaderBundle& ShaderLibrary::Bundle() const noexcept
    {
        return impl->Bundle;
    }

    ShaderLibraryResult<const lodestone::ManifestIndex*> ShaderLibrary::ModuleIndex(std::string_view module_name) const
    {
        ShaderBundleResult<const lodestone::ManifestIndex*> index = impl->Bundle.ModuleIndex(module_name);

        if (!index.has_value())
        {
            return std::unexpected(ToShaderLibraryError(index.error()));
        }

        return *index;
    }

    ShaderLibraryResult<std::unique_ptr<ManifestProgram>> ShaderLibrary::CreateProgram(
        const DeviceHandle& device,
        std::string_view module_name,
        lodestone::VariantKey variant,
        std::span<const std::string_view> entry_points,
        const ManifestProgramOptions& options) const
    {
        if (entry_points.empty())
        {
            return std::unexpected(ShaderLibraryError::NoEntryPointsRequested);
        }

        ShaderBundleResult<const lodestone::manifest::EnvironmentView*> moduleEnvironment =
            impl->Bundle.ModuleEnvironment(module_name);

        if (!moduleEnvironment.has_value())
        {
            return std::unexpected(ToShaderLibraryError(moduleEnvironment.error()));
        }

        const lodestone::manifest::EnvironmentView& environment = **moduleEnvironment;

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

        // Slang puts every stage's push constant block at offset 0, and Merge checked that the blocks agree where they
        // overlap, so one range covers the pipeline (Lodestone C1)
        std::vector<PushConstantRange> pushConstantRanges;
        VkPushConstantRange vkPushConstantRange{};
        if (const std::optional<lodestone::manifest::MergedPushConstants> pushConstants = mergedLayout->PushConstants())
        {
            programImpl->PushConstants = PushConstantRange{ ToRhiShaderStages(pushConstants->Stages), 0u, pushConstants->Constants.ByteSize() };
            pushConstantRanges.push_back(*programImpl->PushConstants);
            vkPushConstantRange = VkPushConstantRange{ ToVkShaderStages(pushConstants->Stages), 0u, pushConstants->Constants.ByteSize() };
        }

        const VkPipelineLayoutCreateInfo pipelineLayoutInfo
        {
            VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            nullptr,
            0,
            static_cast<uint32_t>(vkSetLayouts.size()),
            vkSetLayouts.data(),
            programImpl->PushConstants.has_value() ? 1u : 0u,
            programImpl->PushConstants.has_value() ? &vkPushConstantRange : nullptr
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
        for (size_t i = 0; i < instances.size(); ++i)
        {
            // the cook fails when OpEntryPoint differs from the manifest's entry point name, so the requested name is the SPIR-V name
            const lodestone::manifest::EntryPointInstanceView& instance = instances[i];
            binaryOptions.emplace_back(
                instance.SpirvWords(),
                ToRhiShaderStage(instance.Stage()),
                entry_points[i],
                std::span<PushConstantRange>{ pushConstantRanges },
                std::span<DescriptorSetLayoutHandle>{ programImpl->SetLayouts });
            binaryOptions.back().RequiredSubgroupSize = options.RequiredSubgroupSize;
            binaryOptions.back().RequireFullSubgroups = options.RequireFullSubgroups;
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
    rhi::ShaderLibraryError ToShaderLibraryError(rhi::ShaderBundleError error) noexcept
    {
        switch (error)
        {
            case rhi::ShaderBundleError::FileUnreadable: return rhi::ShaderLibraryError::ManifestFileUnreadable;
            case rhi::ShaderBundleError::BundleInvalid: return rhi::ShaderLibraryError::ManifestInvalid;
            case rhi::ShaderBundleError::ProfileNotInBundle: return rhi::ShaderLibraryError::ProfileNotInManifest;
            case rhi::ShaderBundleError::EnvironmentInvalid: return rhi::ShaderLibraryError::EnvironmentInvalid;
            case rhi::ShaderBundleError::ModuleNotInBundle: return rhi::ShaderLibraryError::ModuleNotInManifest;
            case rhi::ShaderBundleError::Invalid:
            case rhi::ShaderBundleError::Success:
                return rhi::ShaderLibraryError::Invalid;
        }

        return rhi::ShaderLibraryError::Invalid;
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
        constexpr std::pair<lodestone::ShaderStageKind, VkShaderStageFlagBits> k_StageTable[]
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
        for (const auto& [stage, vkStage] : k_StageTable)
        {
            if ((static_cast<uint32_t>(stages) & static_cast<uint32_t>(stage)) != 0u)
            {
                result |= vkStage;
            }
        }
        return result;
    }

    rhi::ShaderStageFlags ToRhiShaderStages(lodestone::ShaderStageKind stages) noexcept
    {
        rhi::ShaderStageFlags result = rhi::ShaderStageFlags::None;
        for (uint32_t bit = 1u; bit < static_cast<uint32_t>(lodestone::ShaderStageKind::Count); bit <<= 1u)
        {
            if ((static_cast<uint32_t>(stages) & bit) != 0u)
            {
                result |= ToRhiShaderStage(static_cast<lodestone::ShaderStageKind>(bit));
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
            if (resource.Placement() == lodestone::PlacementKind::Pointer)
            {
                continue;
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

            sets[setIndex].Bindings.emplace_back(placement.Word1,
                                                 *descriptorType,
                                                 std::max(resource.ArrayCount(),
                                                 1u),
                                                 ToVkShaderStages(merged.Stages),
                                                 nullptr);

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
