#include "GpuBuffers.hpp"
#include "CommandBuffer.hpp"
#include "ManifestDescriptors.hpp"
#include "PlanetGpuErrors.hpp"
#include "PlanetShaders.hpp"
#include "ResourceContext.hpp"
#include "ResourceFlags.hpp"
#include "ResourceFuture.hpp"
#include "ResourceTypes.hpp"
#include "RhiFlags.hpp"
#include "RhiHandle.hpp"
#include "RhiTypes.hpp"
#include "math/MathStorage.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <expected>
#include <optional>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace planet
{
    namespace
    {
        constexpr uint64_t k_TimeoutNs = 10'000'000'000ull;
        // VK_WHOLE_SIZE
        constexpr uint64_t k_WholeBuffer = ~0ull;

        template<typename T>
        [[nodiscard]] PlanetGpuResult<std::vector<T>> ReadTyped(const GpuBuffers& buffers, std::string_view name);
    }

    GpuBuffers::GpuBuffers(ResourceContext& _resources) noexcept :
        resources{ _resources }
    {}

    GpuBuffers::~GpuBuffers()
    {
        for (const Entry& entry : entries)
        {
            for (const GraphicsResource& resource : { entry.Buffer, entry.Readback })
            {
                if (!resource)
                {
                    continue;
                }

                rhi::OperationFuture destroyed = resources.DestroyResource(resource);

                if (resources.Wait(destroyed, k_TimeoutNs) != rhi::FutureState::Complete)
                {
                    std::println(stderr, "GpuBuffers: {} was not destroyed", entry.Name);
                }
            }
        }
    }

    PlanetGpuError GpuBuffers::AddStorage(std::string_view name, std::span<const std::byte> data, bool readable)
    {
        BufferCreateInfo createInfo{};
        createInfo.Domain = ResourceDomain::GPUOnly;
        // a copy destination too, so that the step runner can fill and restore it, and indirect, so that a pass can
        // write the group counts of a later dispatch
        createInfo.Usage = BufferUsageBits::ComputeShaderUAV | BufferUsageBits::CopySource | BufferUsageBits::CopyDestination |
                           BufferUsageBits::IndirectCompute;
        createInfo.Size = data.size();
        createInfo.Name = name;
        PlanetGpuResult<GraphicsResource> buffer = create(createInfo, data);

        if (!buffer.has_value())
        {
            return buffer.error();
        }

        Entry& entry = entries.emplace_back(Entry{ std::string{ name }, *buffer, GraphicsResource::Null(), data.size() });
        bound.emplace(entry.Name, BoundResource{ .Resource = entry.Buffer });

        if (!readable)
        {
            return PlanetGpuError::Success;
        }

        BufferCreateInfo readbackInfo{};
        readbackInfo.Domain = ResourceDomain::GPUToCPU;
        readbackInfo.Usage = BufferUsageBits::CopyDestination;
        readbackInfo.Size = data.size();
        PlanetGpuResult<GraphicsResource> readback = create(readbackInfo, {});

        if (!readback.has_value())
        {
            return readback.error();
        }

        entry.Readback = *readback;
        return PlanetGpuError::Success;
    }

    PlanetGpuError GpuBuffers::AddZeroedStorage(std::string_view name, size_t byte_size, bool readable)
    {
        const std::vector<std::byte> zeros(byte_size, std::byte{ 0 });
        return AddStorage(name, zeros, readable);
    }

    PlanetGpuError GpuBuffers::AddUniform(std::string_view name, std::span<const std::byte> data)
    {
        BufferCreateInfo createInfo{};
        createInfo.Domain = ResourceDomain::CPUToGPU;
        createInfo.Usage = BufferUsageBits::ComputeShaderUBO;
        createInfo.Size = data.size();
        createInfo.Name = name;
        PlanetGpuResult<GraphicsResource> buffer = create(createInfo, data);

        if (!buffer.has_value())
        {
            return buffer.error();
        }

        Entry& entry = entries.emplace_back(Entry{ std::string{ name }, *buffer, GraphicsResource::Null(), data.size() });
        bound.emplace(entry.Name, BoundResource{ .Resource = entry.Buffer });
        return PlanetGpuError::Success;
    }

    PlanetGpuError GpuBuffers::AddUniformFrom(const PlanetShaders& shaders,
                                              std::string_view entry_point,
                                              std::string_view resource,
                                              std::span<const UniformValue> values)
    {
        const PlanetGpuResult<std::vector<std::byte>> bytes = shaders.UniformBytes(entry_point, resource, values);

        if (!bytes.has_value())
        {
            return bytes.error();
        }

        return AddUniform(resource, *bytes);
    }

    const BoundResources& GpuBuffers::Bound() const noexcept
    {
        return bound;
    }

    void GpuBuffers::RecordReadbackCopies(rhi::CommandBuffer& command_buffer) const
    {
        const rhi::GlobalBarrier toCopy
        {
            rhi::PipelineStage::ComputeShader, rhi::Access::ShaderStorageWrite,
            rhi::PipelineStage::Copy, rhi::Access::TransferRead
        };
        command_buffer.Barrier(rhi::DependencyInfo{ std::span{ &toCopy, 1 }, {}, {} });

        for (const Entry& entry : entries)
        {
            if (!entry.Readback)
            {
                continue;
            }

            const rhi::BufferCopy region{ 0u, 0u, entry.ByteSize };
            command_buffer.CopyBuffer(rhi::BufferHandle{ entry.Buffer.VkHandle }, rhi::BufferHandle{ entry.Readback.VkHandle },
                                      std::span{ &region, 1 });
        }

        const rhi::GlobalBarrier toHost
        {
            rhi::PipelineStage::Copy, rhi::Access::TransferWrite,
            rhi::PipelineStage::Host, rhi::Access::HostRead
        };
        command_buffer.Barrier(rhi::DependencyInfo{ std::span{ &toHost, 1 }, {}, {} });
    }

    PlanetGpuError GpuBuffers::RecordFill(rhi::CommandBuffer& command_buffer, std::string_view name, uint32_t value) const
    {
        const Entry* entry = find(name);

        if (entry == nullptr)
        {
            std::println(stderr, "GpuBuffers: no buffer {} to fill", name);
            return PlanetGpuError::BufferNotFound;
        }

        // vkCmdFillBuffer needs a size that is a multiple of 4; VK_WHOLE_SIZE rounds the end down for a byte array
        command_buffer.FillBuffer(rhi::BufferHandle{ entry->Buffer.VkHandle }, 0u, k_WholeBuffer, value);
        return PlanetGpuError::Success;
    }

    PlanetGpuError GpuBuffers::RecordCopy(rhi::CommandBuffer& command_buffer, std::string_view source, std::string_view destination) const
    {
        const Entry* from = find(source);
        const Entry* to = find(destination);

        if (from == nullptr || to == nullptr || from->ByteSize != to->ByteSize)
        {
            std::println(stderr, "GpuBuffers: cannot copy {} to {} (missing, or the sizes differ)", source, destination);
            return PlanetGpuError::BufferNotFound;
        }

        const rhi::BufferCopy region{ 0u, 0u, from->ByteSize };
        command_buffer.CopyBuffer(rhi::BufferHandle{ from->Buffer.VkHandle }, rhi::BufferHandle{ to->Buffer.VkHandle }, std::span{ &region, 1 });
        return PlanetGpuError::Success;
    }

    std::optional<rhi::BufferHandle> GpuBuffers::Handle(std::string_view name) const
    {
        const Entry* entry = find(name);

        if (entry == nullptr)
        {
            return std::nullopt;
        }

        return rhi::BufferHandle{ entry->Buffer.VkHandle };
    }

    PlanetGpuResult<std::vector<std::byte>> GpuBuffers::Read(std::string_view name) const
    {
        const Entry* entry = find(name);

        if (entry == nullptr || !entry->Readback)
        {
            std::println(stderr, "GpuBuffers: {} is not a readable buffer", name);
            return std::unexpected(PlanetGpuError::BufferNotFound);
        }

        rhi::ResourceFuture<void*> mapped = resources.MapBuffer(entry->Readback, entry->ByteSize, 0u);

        if (resources.Wait(mapped, k_TimeoutNs) != rhi::FutureState::Complete)
        {
            std::println(stderr, "GpuBuffers: MapBuffer failed on {}", name);
            return std::unexpected(PlanetGpuError::ReadbackFailed);
        }

        std::vector<std::byte> bytes(entry->ByteSize);
        std::memcpy(bytes.data(), mapped.Get(), entry->ByteSize);
        rhi::OperationFuture unmapped = resources.UnmapBuffer(entry->Readback, entry->ByteSize, 0u);

        if (resources.Wait(unmapped, k_TimeoutNs) != rhi::FutureState::Complete)
        {
            std::println(stderr, "GpuBuffers: UnmapBuffer failed on {}", name);
            return std::unexpected(PlanetGpuError::ReadbackFailed);
        }

        return bytes;
    }

    PlanetGpuResult<std::vector<int32_t>> GpuBuffers::ReadInts(std::string_view name) const
    {
        return ReadTyped<int32_t>(*this, name);
    }

    PlanetGpuResult<std::vector<uint32_t>> GpuBuffers::ReadWords(std::string_view name) const
    {
        return ReadTyped<uint32_t>(*this, name);
    }

    PlanetGpuResult<std::vector<float>> GpuBuffers::ReadFloats(std::string_view name) const
    {
        return ReadTyped<float>(*this, name);
    }

    PlanetGpuResult<std::vector<math::Float4>> GpuBuffers::ReadFloat4s(std::string_view name) const
    {
        return ReadTyped<math::Float4>(*this, name);
    }

    PlanetGpuResult<std::vector<math::Int4>> GpuBuffers::ReadInt4s(std::string_view name) const
    {
        return ReadTyped<math::Int4>(*this, name);
    }

    PlanetGpuResult<GraphicsResource> GpuBuffers::create(const BufferCreateInfo& create_info, std::span<const std::byte> data)
    {
        const RhiBufferResourceData initial{ data, 0u };
        std::span<const RhiBufferResourceData> initialData{};

        if (!data.empty())
        {
            initialData = std::span{ &initial, 1u };
        }

        // Complete means created and, for a device-local buffer, uploaded. The check waits on the CPU so that its
        // submits need no wait on the transfer timeline
        rhi::ResourceFuture<GraphicsResource> created = resources.CreateBuffer(create_info, initialData);

        if (resources.Wait(created, k_TimeoutNs) != rhi::FutureState::Complete)
        {
            std::println(stderr, "GpuBuffers: ResourceContext did not create {} ({} bytes)", create_info.Name, create_info.Size);
            return std::unexpected(PlanetGpuError::BufferCreationFailed);
        }

        return created.Get();
    }

    const GpuBuffers::Entry* GpuBuffers::find(std::string_view name) const noexcept
    {
        const auto found = std::ranges::find(entries, name, &Entry::Name);

        if (found == entries.end())
        {
            return nullptr;
        }

        return &*found;
    }

    namespace
    {
        template<typename T>
        PlanetGpuResult<std::vector<T>> ReadTyped(const GpuBuffers& buffers, std::string_view name)
        {
            static_assert(std::is_trivially_copyable_v<T>);
            const PlanetGpuResult<std::vector<std::byte>> bytes = buffers.Read(name);

            if (!bytes.has_value())
            {
                return std::unexpected(bytes.error());
            }

            if (bytes->size() % sizeof(T) != 0u)
            {
                std::println(stderr, "GpuBuffers: {} byte size {} is not a multiple of {}", name, bytes->size(), sizeof(T));
                return std::unexpected(PlanetGpuError::ReadbackFailed);
            }

            const size_t count = bytes->size() / sizeof(T);
            std::vector<T> values(count);

            if (!values.empty())
            {
                std::memcpy(values.data(), bytes->data(), bytes->size());
            }

            return values;
        }
    }
}
