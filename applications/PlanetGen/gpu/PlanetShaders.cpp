#include "PlanetShaders.hpp"
#include "CommandBuffer.hpp"
#include "Device.hpp"
#include "DeviceStage.hpp"
#include "RhiAssert.hpp"
#include "RhiFlags.hpp"
#include "RhiHandle.hpp"
#include "RhiTypes.hpp"
#include "ShaderLibrary.hpp"
#include "ShaderManifest.hpp"
#include "ShaderManifestIndex.hpp"
#include "ShaderProgram.hpp"
#include "VariantKey.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cstring>
#include <print>
#include <string>
#include <vulkan/vulkan_core.h>

namespace planet
{
    namespace
    {
        // generous: one set for each entry point, and no PlanetLab pass binds more than 24 buffers
        constexpr uint32_t k_StorageBuffersPerSet = 24u;
        constexpr uint32_t k_UniformBuffersPerSet = 4u;

        [[nodiscard]] lodestone::ManifestQueryBuilder ConstrainCrustFlags(const lodestone::ManifestQueryBuilder& query,
                                                                          CrustFlagChoice choice) noexcept
        {
            switch (choice)
            {
            case CrustFlagChoice::Bytes:
                return query.Where(k_CrustFlagAxisName, true);
            case CrustFlagChoice::Words:
                return query.Where(k_CrustFlagAxisName, false);
            default:
                // the byte variant needs a superset of the capabilities of the word variant, so it dominates
                return query.PreferCapable();
            }
        }

        // the value of the crust flag axis in `variant`, read back from the key
        [[nodiscard]] CrustFlagStorage DecodeCrustFlags(const lodestone::ManifestIndex& index, lodestone::VariantKey variant)
        {
            const std::vector<std::string_view> axisNames = index.AxisNames();
            const std::vector<lodestone::QueryAxisValue> values = index.Decode(variant);
            const auto found = std::ranges::find(axisNames, k_CrustFlagAxisName);

            if (found == axisNames.end())
            {
                return CrustFlagStorage::Invalid;
            }

            const size_t axis = static_cast<size_t>(found - axisNames.begin());
            return values[axis].IntegralValue != 0u ? CrustFlagStorage::Bytes : CrustFlagStorage::Words;
        }
    }

    struct PlanetShadersImpl
    {
        struct Pass
        {
            std::string_view EntryPoint;
            std::unique_ptr<rhi::ManifestProgram> Program;
        };

        VkDevice Device{ VK_NULL_HANDLE };
        const lodestone::ManifestIndex* Index{ nullptr };
        lodestone::VariantKey Variant{};
        CrustFlagStorage CrustFlags{ CrustFlagStorage::Invalid };
        std::vector<Pass> Passes;

        [[nodiscard]] const Pass* Find(std::string_view entry_point) const noexcept;
    };

    struct PassBindingsImpl
    {
        VkDevice Device{ VK_NULL_HANDLE };
        VkDescriptorPool Pool{ VK_NULL_HANDLE };
        // in the sequence of PlanetShadersImpl::Passes
        std::vector<VkDescriptorSet> Sets;

        ~PassBindingsImpl();
    };

    CrustFlagBuffers CrustFlagBufferNames(CrustFlagStorage storage) noexcept
    {
        if (storage == CrustFlagStorage::Bytes)
        {
            return CrustFlagBuffers{ "CrustAliveBytes", "CrustContinentalBytes" };
        }

        return CrustFlagBuffers{ "CrustAliveWords", "CrustContinentalWords" };
    }

    std::string_view ToString(CrustFlagStorage storage) noexcept
    {
        switch (storage)
        {
        case CrustFlagStorage::Bytes:
            return "bytes";
        case CrustFlagStorage::Words:
            return "words";
        default:
            return "invalid";
        }
    }

    UniformValue UniformFloat(std::string_view member, float value) noexcept
    {
        return UniformValue{ member, std::bit_cast<uint32_t>(value) };
    }

    UniformValue UniformUint(std::string_view member, uint32_t value) noexcept
    {
        return UniformValue{ member, value };
    }

    const PlanetShadersImpl::Pass* PlanetShadersImpl::Find(std::string_view entry_point) const noexcept
    {
        const auto found = std::ranges::find(Passes, entry_point, &Pass::EntryPoint);

        if (found == Passes.end())
        {
            return nullptr;
        }

        return &*found;
    }

    PassBindingsImpl::~PassBindingsImpl()
    {
        // destroying the pool frees its sets
        if (Pool != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorPool(Device, Pool, nullptr);
        }
    }

    PassBindings::PassBindings(std::unique_ptr<PassBindingsImpl>&& _impl) noexcept :
        impl{ std::move(_impl) }
    {}

    PassBindings::~PassBindings() = default;

    PlanetGpuResult<std::unique_ptr<PlanetShaders>> PlanetShaders::Create(rhi::Device& device,
                                                                          const rhi::ShaderLibrary& library,
                                                                          const bootstrap::DeviceFilter& filter,
                                                                          CrustFlagChoice crust_flags,
                                                                          std::span<const std::string_view> entry_points)
    {
        auto impl = std::make_unique<PlanetShadersImpl>();
        impl->Device = device.Handle().As<VkDevice>();
        rhi::ShaderLibraryResult<const lodestone::ManifestIndex*> index = library.ModuleIndex(k_PlanetModuleName);

        if (!index.has_value())
        {
            std::println(stderr, "PlanetShaders: module {}: {}", k_PlanetModuleName, rhi::ToString(index.error()));
            return std::unexpected(PlanetGpuError::ModuleNotFound);
        }

        impl->Index = *index;
        const lodestone::ManifestQueryBuilder deviceQuery = impl->Index->Query().ForDevice(filter.Supports, filter.User, filter.Limits);
        std::expected<lodestone::VariantKey, lodestone::QueryError> variant = ConstrainCrustFlags(deviceQuery, crust_flags).SingleFor(entry_points);

        if (!variant.has_value() && variant.error().Code == lodestone::QueryErrorCode::NoVariantFitsDevice)
        {
            // the caller decides whether a storage that the device lacks is a failure
            return std::unexpected(PlanetGpuError::NoVariantFitsDevice);
        }

        if (!variant.has_value())
        {
            std::println(stderr, "PlanetShaders: no single variant (query error {}, axis '{}')",
                         static_cast<int32_t>(variant.error().Code), variant.error().AxisName);
            return std::unexpected(PlanetGpuError::VariantNotFound);
        }

        impl->Variant = *variant;
        impl->CrustFlags = DecodeCrustFlags(*impl->Index, *variant);

        if (impl->CrustFlags == CrustFlagStorage::Invalid)
        {
            std::println(stderr, "PlanetShaders: the module has no axis {}", k_CrustFlagAxisName);
            return std::unexpected(PlanetGpuError::VariantNotFound);
        }

        impl->Passes.reserve(entry_points.size());

        for (const std::string_view entryPoint : entry_points)
        {
            const std::array<std::string_view, 1> names{ entryPoint };
            rhi::ShaderLibraryResult<std::unique_ptr<rhi::ManifestProgram>> program =
                library.CreateProgram(device.Handle(), k_PlanetModuleName, impl->Variant, names);

            if (!program.has_value())
            {
                std::println(stderr, "PlanetShaders: {}: {}", entryPoint, rhi::ToString(program.error()));
                return std::unexpected(PlanetGpuError::ProgramCreationFailed);
            }

            impl->Passes.push_back(PlanetShadersImpl::Pass{ entryPoint, std::move(*program) });
        }

        return std::make_unique<PlanetShaders>(std::move(impl));
    }

    PlanetShaders::PlanetShaders(std::unique_ptr<PlanetShadersImpl>&& _impl) noexcept :
        impl{ std::move(_impl) }
    {}

    PlanetShaders::~PlanetShaders() = default;

    CrustFlagStorage PlanetShaders::CrustFlags() const noexcept
    {
        return impl->CrustFlags;
    }

    PlanetGpuResult<std::vector<std::byte>> PlanetShaders::UniformBytes(std::string_view entry_point,
                                                                        std::string_view resource,
                                                                        std::span<const UniformValue> values) const
    {
        const lodestone::manifest::EnvironmentView& environment = impl->Index->View();
        lodestone::manifest::ManifestResult<uint32_t> entryIndex = environment.Module().FindEntryPointIndex(entry_point);
        std::optional<lodestone::manifest::VariantView> variantView = environment.VariantByKey(impl->Variant);

        if (!entryIndex.has_value() || !variantView.has_value())
        {
            std::println(stderr, "PlanetShaders: entry point {} is not in the variant", entry_point);
            return std::unexpected(PlanetGpuError::EntryPointNotFound);
        }

        const lodestone::manifest::EntryPointInstanceView instance = variantView->EntryPoint(*entryIndex);

        for (const lodestone::manifest::ResolvedResource reflected : instance.Layout())
        {
            if (reflected.Name() != resource)
            {
                continue;
            }

            // a uniform buffer is at least 16 bytes in std140
            std::vector<std::byte> bytes(std::max<size_t>(static_cast<size_t>(reflected.ByteSize()), 16u));
            int32_t written = 0;

            for (const UniformValue& value : values)
            {
                for (const lodestone::manifest::UniformMemberView member : reflected.Members())
                {
                    if (!member.Name().ends_with(value.Member))
                    {
                        continue;
                    }

                    std::memcpy(bytes.data() + member.Offset(), &value.Bits, sizeof(value.Bits));
                    ++written;
                    break;
                }
            }

            if (written != std::ssize(values))
            {
                std::println(stderr, "PlanetShaders: {} in {} lacks a member that the host writes", resource, entry_point);
                return std::unexpected(PlanetGpuError::UniformNotFound);
            }

            return bytes;
        }

        std::println(stderr, "PlanetShaders: {} does not read the uniform buffer {}", entry_point, resource);
        return std::unexpected(PlanetGpuError::UniformNotFound);
    }

    PlanetGpuResult<std::unique_ptr<PassBindings>> PlanetShaders::Bind(const BoundResources& resources) const
    {
        auto bindings = std::make_unique<PassBindingsImpl>();
        bindings->Device = impl->Device;
        const uint32_t setCount = static_cast<uint32_t>(impl->Passes.size());
        const std::array<VkDescriptorPoolSize, 2> poolSizes
        {
            VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, k_StorageBuffersPerSet * setCount },
            VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, k_UniformBuffersPerSet * setCount },
        };
        const VkDescriptorPoolCreateInfo poolInfo
        {
            VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr, 0, setCount,
            static_cast<uint32_t>(poolSizes.size()), poolSizes.data()
        };

        if (vkCreateDescriptorPool(impl->Device, &poolInfo, nullptr, &bindings->Pool) != VK_SUCCESS)
        {
            return std::unexpected(PlanetGpuError::DescriptorPoolFailed);
        }

        for (const PlanetShadersImpl::Pass& pass : impl->Passes)
        {
            const VkDescriptorSetLayout setLayout = pass.Program->SetLayouts()[0].As<VkDescriptorSetLayout>();
            const VkDescriptorSetAllocateInfo setInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, bindings->Pool, 1, &setLayout };
            VkDescriptorSet set = VK_NULL_HANDLE;

            if (vkAllocateDescriptorSets(impl->Device, &setInfo, &set) != VK_SUCCESS)
            {
                return std::unexpected(PlanetGpuError::DescriptorPoolFailed);
            }

            const std::array<std::string_view, 1> names{ pass.EntryPoint };
            std::optional<std::string> error = WriteManifestDescriptors(impl->Device, set, *impl->Index, impl->Variant, names, resources);

            if (error.has_value())
            {
                std::println(stderr, "PlanetShaders: {}: {}", pass.EntryPoint, *error);
                return std::unexpected(PlanetGpuError::DescriptorWriteFailed);
            }

            bindings->Sets.push_back(set);
        }

        return std::make_unique<PassBindings>(std::move(bindings));
    }

    void PlanetShaders::RecordDispatch(rhi::CommandBuffer& command_buffer,
                                       const PassBindings& bindings,
                                       std::string_view entry_point,
                                       uint32_t thread_count) const
    {
        const PlanetShadersImpl::Pass* pass = impl->Find(entry_point);
        // a name that Create did not get is a programming error
        assert(pass != nullptr);
        const size_t passIndex = static_cast<size_t>(pass - impl->Passes.data());
        const rhi::DescriptorSetHandle set{ reinterpret_cast<uint64_t>(bindings.impl->Sets[passIndex]) };
        const uint32_t workgroupX = pass->Program->Workgroup()[0];

        RhiAssert(command_buffer.BindShaders(pass->Program->Program()));
        command_buffer.BindDescriptorSets(rhi::PipelineBindPoint::Compute, pass->Program->PipelineLayout(), 0, std::span{ &set, 1 });
        command_buffer.Dispatch((thread_count + workgroupX - 1u) / workgroupX, 1, 1);
    }

    void RecordComputeBarrier(rhi::CommandBuffer& command_buffer)
    {
        const rhi::GlobalBarrier barrier
        {
            rhi::PipelineStage::ComputeShader, rhi::Access::ShaderStorageWrite,
            rhi::PipelineStage::ComputeShader, rhi::Access::ShaderStorageRead | rhi::Access::ShaderStorageWrite
        };
        command_buffer.Barrier(rhi::DependencyInfo{ std::span{ &barrier, 1 }, {}, {} });
    }
}
